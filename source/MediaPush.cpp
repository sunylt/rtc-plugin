
//#define _HAS_BYTE 0
//#include <Windows.h>
//#include <atlbase.h>
#include "MediaPush.h"
#include <thread>
#include <chrono>
#include <timeapi.h>
#include "Tracer.h"

#pragma comment(lib, "winmm.lib")


const wchar_t kSharedVideoMemName[] = L"RTCVideoSharedMemory";
const wchar_t kSharedAudioMemName[] = L"RTCAudioSharedMemory";


const wchar_t eventStart[] = L"Start_Push_RTC";
const wchar_t eventStop[] = L"Stop_Push_RTC";
HANDLE g_startPush = nullptr;
HANDLE g_stopPush = nullptr;


AudioDelayBuffer::AudioDelayBuffer(int delayMs, size_t maxFrames)
	: m_delayMs(delayMs), m_maxFrames(maxFrames)
{
}

bool AudioDelayBuffer::push(const uint8_t* data, size_t size, int64_t renderTimeMs, int64_t nowMs)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if (data == nullptr || size == 0 || m_delayMs <= 0 || size > 1920) {
		return false;
	}

	if (m_queue.size() >= m_maxFrames) {
		m_queue.pop_front();
	}

	AudioFrameItem item;
	item.buffer.assign(data, data + size);
	item.size = size;
	item.renderTimeMs = renderTimeMs;
	item.enqueueTimeMs = nowMs;
	m_queue.push_back(std::move(item));
	return true;
}

bool AudioDelayBuffer::pop(AudioFrameItem& outFrame, int64_t nowMs)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if (m_queue.empty()) {
		return false;
	}

	const auto& front = m_queue.front();
	int64_t releaseTime = front.renderTimeMs + m_delayMs;

	if (nowMs >= releaseTime) {
		outFrame = std::move(m_queue.front());
		m_queue.pop_front();
		return true;
	}

	return false;
}

void AudioDelayBuffer::clear()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_queue.clear();
}

size_t AudioDelayBuffer::size() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_queue.size();
}

size_t AudioDelayBuffer::capacity() const
{
	return m_maxFrames;
}

void AudioDelayBuffer::setDelayMs(int delayMs)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_delayMs = std::max(0, delayMs);
}

int AudioDelayBuffer::getDelayMs() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_delayMs;
}

VideoDelayBuffer::VideoDelayBuffer(int delayMs, size_t maxFrames)
	: m_delayMs(delayMs), m_maxFrames(maxFrames)
{
}

bool VideoDelayBuffer::push(VideoFrameItem&& item, int64_t nowMs)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if (m_delayMs <= 0 || item.pixel.empty()) {
		return false;
	}

	if (m_queue.size() >= m_maxFrames) {
		m_queue.pop_front();
	}

	item.enqueueTimeMs = nowMs;
	m_queue.push_back(std::move(item));
	return true;
}

bool VideoDelayBuffer::pop(VideoFrameItem& outFrame, int64_t nowMs)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if (m_queue.empty()) {
		return false;
	}

	const auto& front = m_queue.front();
	int64_t releaseTime = front.renderTimeMs + m_delayMs;

	if (nowMs >= releaseTime) {
		outFrame = std::move(m_queue.front());
		m_queue.pop_front();
		return true;
	}

	return false;
}

void VideoDelayBuffer::clear()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_queue.clear();
}

size_t VideoDelayBuffer::size() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_queue.size();
}

size_t VideoDelayBuffer::capacity() const
{
	return m_maxFrames;
}

void VideoDelayBuffer::setDelayMs(int delayMs)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_delayMs = std::max(0, delayMs);
}


int64_t get_time_stamp()
{
	std::chrono::steady_clock::duration d = std::chrono::steady_clock::now().time_since_epoch();
	std::chrono::milliseconds mic = std::chrono::duration_cast<std::chrono::milliseconds>(d);
	//��ǰʱ���ms ��λ
	return mic.count();
}

MediaPushEngine::MediaPushEngine(agora::rtc::IRtcEngine *rtc_engine,
                                 track_id_t audioId, int colorSpace, int audioDelayMs)
    : m_rtcEngine(rtc_engine),
      m_audioId(audioId),
	  m_obsColorSpace(colorSpace),
	  m_audioDelayMs(std::max(0, audioDelayMs))
{
    wstring log(L"---------MediaPushEngine----------audioId------");
	log += to_wstring(audioId);
	log += L", delayMs=";
	log += to_wstring(audioDelayMs);
    OutputDebugStrW(log.c_str());
	m_obsVideo = {0};
	m_obsAudio = {0};
    m_bInitialize = false;
}

MediaPushEngine ::~MediaPushEngine()
{
	// StopThreads 内部会保证 start/stop 事件严格成对，不会重复发 stop。
	StopThreads();
	if (g_startPush != nullptr) {
		CloseHandle(g_startPush);
		g_startPush = nullptr;
	}
	if (g_stopPush != nullptr) {
		CloseHandle(g_stopPush);
		g_stopPush = nullptr;
	}
	m_rtcEngine = nullptr;
}


bool MediaPushEngine::Initialize(track_id_t audioId, int colorSpace)
{
	StopThreads();
	m_bInitialize = false;
	wstring log(L"---------MediaPushEngine----------InitEvent------");
	if (g_startPush != nullptr) {
		CloseHandle(g_startPush);
		g_startPush = nullptr;
	}
	if (g_stopPush != nullptr) {
		CloseHandle(g_stopPush);
		g_stopPush = nullptr;
	}
	g_startPush = OpenEvent(EVENT_MODIFY_STATE, FALSE, eventStart);
	if (g_startPush == NULL) {

		log = L"OpenEvent   g_startPush  error";
		log += to_wstring(GetLastError());
		OutputDebugStrW(log.c_str());
		return false;
	}
	g_stopPush = OpenEvent(EVENT_MODIFY_STATE, FALSE, eventStop);
	if (g_stopPush == NULL) {

		log = L"OpenEvent   g_stopPush  error";
		log += to_wstring(GetLastError());
		OutputDebugStrW(log.c_str());
		CloseHandle(g_startPush);
		g_startPush = nullptr;
		return false;
	}
	m_audioId = audioId;
	m_obsColorSpace = colorSpace;
	{
		std::lock_guard<std::mutex> lock(m_tsMutex);
		m_sharedTsOffset = -1;
	}
	m_bInitialize = true;
	return true;
}

void MediaPushEngine::SetAudioDelay(int delayMs)
{
	std::lock_guard<std::mutex> lock(m_stateMutex);
	m_requestedDelayMs = delayMs;

	if (delayMs > 0) {
		// 正延迟：让声音延后，画面即时推送
		m_audioDelayMs = delayMs;
		m_videoDelayMs = 0;
		m_videoDelayBuffer.reset();
		if (m_audioDelayBuffer == nullptr) {
			const size_t capacity = std::max<size_t>(500, (static_cast<size_t>(delayMs) / 10) + 50);
			m_audioDelayBuffer = std::make_unique<AudioDelayBuffer>(delayMs, capacity);
		} else {
			const size_t needed = std::max<size_t>(500, (static_cast<size_t>(delayMs) / 10) + 50);
			if (needed > m_audioDelayBuffer->capacity()) {
				m_audioDelayBuffer = std::make_unique<AudioDelayBuffer>(delayMs, needed);
			} else {
				m_audioDelayBuffer->setDelayMs(delayMs);
				m_audioDelayBuffer->clear();
			}
		}
	} else if (delayMs < 0) {
		// 负延迟：声音提前（即画面延后），声音即时推送、画面走延迟缓冲
		const int vd = -delayMs;
		m_videoDelayMs = vd;
		m_audioDelayMs = 0;
		m_audioDelayBuffer.reset();
		// 按 30fps 估算帧间隔 ~33ms，容量 = 延迟帧数 + 少量余量
		const size_t capacity = std::max<size_t>(4, (static_cast<size_t>(vd) / 33) + 4);
		if (m_videoDelayBuffer == nullptr) {
			m_videoDelayBuffer = std::make_unique<VideoDelayBuffer>(vd, capacity);
		} else {
			if (capacity > m_videoDelayBuffer->capacity()) {
				m_videoDelayBuffer = std::make_unique<VideoDelayBuffer>(vd, capacity);
			} else {
				m_videoDelayBuffer->setDelayMs(vd);
				m_videoDelayBuffer->clear();
			}
		}
	} else {
		// 零延迟：两侧都不走缓冲
		m_audioDelayMs = 0;
		m_videoDelayMs = 0;
		m_audioDelayBuffer.reset();
		m_videoDelayBuffer.reset();
	}
}

int MediaPushEngine::GetAudioDelay() const
{
	std::lock_guard<std::mutex> lock(m_stateMutex);
	return m_requestedDelayMs;
}

bool MediaPushEngine::EnablePlugin() 
{
  if (m_rtcEngine == nullptr || !m_bInitialize) {
      return false;
  }

  if (m_bStartPush.load()) {
      return true;
  }

  // A worker can have stopped because of an exception while its std::thread
  // remains joinable. Reap it before starting a new generation.
  if (m_videoThread.joinable() || m_audioThread.joinable()) {
      StopThreads();
  }

  int ret = m_rtcEngine->queryInterface(agora::rtc::AGORA_IID_MEDIA_ENGINE,
                                        reinterpret_cast<void **>(&m_mediaEngine));
  if (ret != 0 || m_mediaEngine == nullptr) {
      m_mediaEngine = nullptr;
      return false;
  }

  OutputDebugStrW(L"--------EnablePlugin---------");
  // 每次真正启动前把推流帧结构体重置为默认构造，避免上一周期残留的
  // crop/rotation/metadata/alpha 等字段影响本周期推送。
  m_videoFrame = agora::media::base::ExternalVideoFrame();
  m_audioFrame = agora::media::IAudioFrameObserver::AudioFrame();
  m_bStartPush.store(true);
  // 真正的“启动”信号：只有 EnablePlugin 真正开始运行时才通知 OBS 恢复写帧。
  // 停止信号由 SignalObsStop 通过 m_obsStarted 保证成对且只发一次。
  if (g_startPush != nullptr) {
    m_obsStarted.store(true);
    SetEvent(g_startPush);
  }
  // 提高定时器精度，否则 Sleep(1)/Sleep(5) 实际粒度约为 15.6ms，
  // 音频 10ms 一帧时读取节奏跟不上，会丢帧导致声音卡顿。
  if (timeBeginPeriod(1) == TIMERR_NOERROR) {
      m_timerResolutionSet = true;
  }
  try {
      ReadVideoFromMemory();
      ReadAudioFromMemory();
  } catch (const std::exception& e) {
      OutputDebugStringA(e.what());
      // start 事件已发出，失败路径必须补发 stop，保持事件配对。
      SignalObsStop();
      StopThreads();
      return false;
  }
	return true;
}

bool MediaPushEngine::DisablePlugin()
{
	// 幂等：StopThreads 内部通过 m_obsStarted 保证 stop 事件只发一次，
	// 重复调用不会打破信号配对。
	StopThreads();
	return true;
}

void MediaPushEngine::SignalObsStop()
{
	if (m_obsStarted.exchange(false) && g_stopPush != nullptr) {
		SetEvent(g_stopPush);
	}
}

void MediaPushEngine::StopThreads()
{
    m_bStartPush.store(false);
    if (m_videoThread.joinable()) {
        m_videoThread.join();
    }
    if (m_audioThread.joinable()) {
        m_audioThread.join();
    }
    if (m_obsVideo.buffer != nullptr) {
        delete[] m_obsVideo.buffer;
        m_obsVideo.buffer = nullptr;
    }
    delete[] m_audioBuffer;
    m_audioBuffer = nullptr;
    if (m_timerResolutionSet) {
        timeEndPeriod(1);
        m_timerResolutionSet = false;
    }
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_audioDelayBuffer.reset();
        m_videoDelayBuffer.reset();
    }
    // 复位共享时间戳锚点：Disable 后若不做二次 Initialize，下次 Enable 时
    // MapTimestamp 会基于旧的锚点映射，导致时间戳残留错乱。
    {
        std::lock_guard<std::mutex> lock(m_tsMutex);
        m_sharedTsOffset = -1;
    }
    // 工作线程已全部退出后通知 OBS 停止，保证 start/stop 成对。
    SignalObsStop();
    m_mediaEngine = nullptr;
}

int64_t MediaPushEngine::NowMonotonic() const
{
	return m_rtcEngine != nullptr
		? m_rtcEngine->getCurrentMonotonicTimeInMs()
		: get_time_stamp();
}

// 将源时间戳映射到 Agora 单调时钟域。音频/视频共享同一个映射锚点，
// 避免两路首帧到达时间不同导致固定音画偏移（注释说明已更新）。
int64_t MediaPushEngine::MapTimestamp(uint64_t sourceTimestamp)
{
	if (sourceTimestamp == 0) {
		return NowMonotonic();
	}
	std::lock_guard<std::mutex> lock(m_tsMutex);
	const int64_t now = NowMonotonic();
	const int64_t src = static_cast<int64_t>(sourceTimestamp);
	if (m_sharedTsOffset == -1) {
		m_sharedTsOffset = now - src;
	}
	int64_t ts = src + m_sharedTsOffset;
	// 防止源时钟与 Agora 时钟速率不一致导致延迟持续累积：
	// 映射结果超前当前时间超过阈值时重建锚点。
	if (ts > now + 1000) {
		static int reanchorCount = 0;
		if (reanchorCount++ % 100 == 0) {
			OutputDebugStrW(L"MapTimestamp re-anchored: source clock ahead");
		}
		m_sharedTsOffset = now - src;
		ts = now;
	}
	return ts;
}

void MediaPushEngine::ReadVideoFromMemory()
{
  if (m_mediaEngine == nullptr) { 
      return;
  }

	  m_videoThread = std::thread([this] {
	    try {
	    // 共享内存可能尚未由 OBS 侧创建：持续重试而不是直接退出线程，
	    // 避免 EnablePlugin 先于 OBS 启动时音视频彻底静默。
	    std::unique_ptr<SharedMemory> sMemory;
	    int openFailCount = 0;
	    while (m_bStartPush && !sMemory) {
	      try {
	        sMemory = std::make_unique<SharedMemory>(kSharedVideoMemName, VideoData, true);
	      } catch (const std::exception&) {
	        if (openFailCount++ % 20 == 0) {
	          OutputDebugStrW(L"--------wait RTCVideoSharedMemory---------");
	        }
	        Sleep(500);
	      }
	    }
	    if (!sMemory) {
	      return;
	    }
	OutputDebugStrW(L"--------ReadVideoFromMemory---------");
	m_obsVideo.buffer = new char[3840 * 2160 * 4];
	// 负延迟场景：StopThreads 会 reset 掉 m_videoDelayBuffer，
	// 重新 Enable 后需要在这里延迟创建，否则负延迟失效。
	{
		std::lock_guard<std::mutex> lock(m_stateMutex);
		if (m_videoDelayMs > 0 && m_videoDelayBuffer == nullptr) {
			const size_t capacity = std::max<size_t>(4, (static_cast<size_t>(m_videoDelayMs) / 33) + 4);
			m_videoDelayBuffer = std::make_unique<VideoDelayBuffer>(m_videoDelayMs, capacity);
		}
	}
	//agora::media::base::ExternalVideoFrame rtcFrame;
	static int videoCount = 0;
	agora::media::base::ColorSpace colorSpace;
	bool formatLogged = false;
	int badFrameCount = 0;

	if (m_obsColorSpace == 1) 
	{
		//601
	    colorSpace.matrix = agora::media::base::ColorSpace::MatrixID::MATRIXID_SMPTE170M;
	    colorSpace.range = agora::media::base::ColorSpace::RangeID::RANGEID_LIMITED;
	}
	else
	{
		colorSpace.matrix = agora::media::base::ColorSpace::MatrixID::MATRIXID_BT709;
	    colorSpace.range = agora::media::base::ColorSpace::RangeID::RANGEID_FULL;
	}

    while (m_bStartPush) {
      Sleep(5);
      int64_t now = NowMonotonic();

      // 延迟队列出队独立于读取流程，避免读取失败/丢帧导致缓冲帧一直滞留。
      // nullptr 检查必须在锁内，否则与 SetAudioDelay 中 reset() 存在 TOCTOU 竞争。
      {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (m_videoDelayMs > 0 && m_videoDelayBuffer != nullptr) {
        VideoFrameItem item;
        while (m_videoDelayBuffer->pop(item, now)) {
          m_videoFrame.buffer = item.pixel.data();
          m_videoFrame.timestamp = item.renderTimeMs + m_videoDelayMs;
          m_videoFrame.stride = item.stride;
          m_videoFrame.height = item.height;
          m_videoFrame.format = (agora::media::base::VIDEO_PIXEL_FORMAT) item.format;
          m_videoFrame.type = agora::media::base::ExternalVideoFrame::VIDEO_BUFFER_RAW_DATA;
          m_videoFrame.colorSpace = item.colorSpace;
          if (videoCount % 30 == 0) {
            wstring log(L"---------pushVideoFrame(delayed)----------timestamp------");
            log += to_wstring(m_videoFrame.timestamp);
            log += L", queueSize=";
            log += to_wstring(m_videoDelayBuffer->size());
            OutputDebugStrW(log.c_str());
          }
          videoCount++;
          int vret = m_mediaEngine->pushVideoFrame(&m_videoFrame /*, this->m_videoId*/);
          if (vret != 0 && videoCount % 30 == 0) {
            wstring log(L"---------pushVideoFrame(delayed) failed, ret=");
            log += to_wstring(vret);
            OutputDebugStrW(log.c_str());
          }
        }
        }
      }

      if (sMemory->isDataReady(VideoData)) {
        bool bRet = sMemory->readData(m_obsVideo.buffer, m_obsVideo.size,
                                     m_obsVideo.width, m_obsVideo.height,
                                     m_obsVideo.format, m_obsVideo.timestamp);

        if (!bRet || m_obsVideo.size == 0 || m_obsVideo.size > 3840 * 2160 * 4) {
          continue;
        }

        // 元信息诊断：首帧记录实际宽高/格式/大小，格式不符只告警不丢帧，
        // 避免因假设不符把原本可用的流打断。
        if (!formatLogged) {
          wchar_t metaLog[MAX_PATH]{0};
          swprintf_s(metaLog, L"video frame meta: w=%u h=%u format=%u size=%u",
                     m_obsVideo.width, m_obsVideo.height, m_obsVideo.format, m_obsVideo.size);
          OutputDebugStrW(metaLog);
          formatLogged = true;
        }
        const uint32_t fmt = m_obsVideo.format;
        bool sizeValid = false;
        if (fmt == agora::media::base::VIDEO_PIXEL_NV12) {
            sizeValid = (m_obsVideo.size == m_obsVideo.width * m_obsVideo.height * 3 / 2);
        } else if (fmt == agora::media::base::VIDEO_PIXEL_BGRA ||
                   fmt == agora::media::base::VIDEO_PIXEL_RGBA) {
            sizeValid = (m_obsVideo.size == m_obsVideo.width * m_obsVideo.height * 4);
        }
        if (!sizeValid) {
          if (badFrameCount++ % 30 == 0) {
            wchar_t badLog[MAX_PATH]{0};
            swprintf_s(badLog,
                       L"video frame format unexpected (still pushing): w=%u h=%u format=%u size=%u",
                       m_obsVideo.width, m_obsVideo.height, fmt, m_obsVideo.size);
            OutputDebugStrW(badLog);
          }
        }

        const int64_t mappedTs = MapTimestamp(m_obsVideo.timestamp);
        // nullptr 检查必须在锁内，否则与 SetAudioDelay 中 reset() 存在 TOCTOU 竞争。
        {
          std::lock_guard<std::mutex> lock(m_stateMutex);
          if (m_videoDelayMs > 0 && m_videoDelayBuffer != nullptr) {
            // 负延迟：画面延后，拷贝帧数据入队，稍后按 renderTimeMs+delay 释放。
            VideoFrameItem item;
            item.pixel.assign(m_obsVideo.buffer, m_obsVideo.buffer + m_obsVideo.size);
            item.renderTimeMs = mappedTs;
            item.width = m_obsVideo.width;
            item.height = m_obsVideo.height;
            item.stride = m_obsVideo.width;
            item.format = m_obsVideo.format;
            item.colorSpace = colorSpace;
            m_videoDelayBuffer->push(std::move(item), now);
          } else {
          // 零/正延迟：画面即时推送。
          m_videoFrame.timestamp = mappedTs;
          m_videoFrame.buffer = m_obsVideo.buffer;
          m_videoFrame.stride = m_obsVideo.width;
          m_videoFrame.height = m_obsVideo.height;
          //agora::media::base::VIDEO_PIXEL_BGRA;
          m_videoFrame.format = (agora::media::base::VIDEO_PIXEL_FORMAT) m_obsVideo.format;
          m_videoFrame.type = agora::media::base::ExternalVideoFrame::VIDEO_BUFFER_RAW_DATA;
          m_videoFrame.colorSpace = colorSpace;
          if (videoCount % 30 == 0) {
            wstring log(L"---------m_mediaEngine->pushVideoFrame----------timestamp------");
            log += to_wstring(m_videoFrame.timestamp);
            OutputDebugStrW(log.c_str());
          }
          videoCount++;
          int vret = m_mediaEngine->pushVideoFrame(&m_videoFrame /*, this->m_videoId*/);
          if (vret != 0 && videoCount % 30 == 0) {
            wstring log(L"---------pushVideoFrame failed, ret=");
            log += to_wstring(vret);
            OutputDebugStrW(log.c_str());
          }
        }
        }
      }
    }
    } catch (const std::exception& e) {
      OutputDebugStringA(e.what());
      m_bStartPush.store(false);
      // 线程异常退出时也必须通知 OBS 停止写帧，保持事件配对。
      SignalObsStop();
    }
  });
}

void MediaPushEngine::ReadAudioFromMemory()
{
	if (m_mediaEngine == nullptr) {
		return;
	}

	m_audioThread = std::thread([this] {
		try {
		// 共享内存可能尚未由 OBS 侧创建：持续重试而不是直接退出线程。
		std::unique_ptr<SharedMemory> sMemory;
		int openFailCount = 0;
		while (m_bStartPush && !sMemory) {
			try {
				sMemory = std::make_unique<SharedMemory>(kSharedAudioMemName, AudioData, true);
			} catch (const std::exception&) {
				if (openFailCount++ % 20 == 0) {
					OutputDebugStrW(L"--------wait RTCAudioSharedMemory---------");
				}
				Sleep(500);
			}
		}
		if (!sMemory) {
			return;
		}
		OutputDebugStrW(L"--------ReadAudioFromMemory---------");
		//this->m_obsAudio.buffer = new char[480 * 4];
		constexpr uint32_t kAudioFrameBytes = 480 * 2 * 2; // 480 samples * 2ch * 16bit = 10ms
		m_audioBuffer = new unsigned char[kAudioFrameBytes];
		memset(m_audioBuffer, 0, kAudioFrameBytes);
		memset(m_obsAudio.buffer, 0, kAudioFrameBytes);
		m_audioFrame.channels = 2;
		m_audioFrame.samplesPerChannel = 480;
		m_audioFrame.samplesPerSec = 48000;
		m_audioFrame.bytesPerSample = TWO_BYTES_PER_SAMPLE;
		m_audioFrame.type = agora::media::IAudioFrameObserver::FRAME_TYPE_PCM16;
		static int audioCount = 0;
		int badSizeCount = 0;
		bool sizeLogged = false;

		{
			std::lock_guard<std::mutex> lock(m_stateMutex);
			if (m_audioDelayMs > 0 && m_audioDelayBuffer == nullptr) {
				const size_t frameDurationMs = 10;
				const size_t capacity = std::max<size_t>(500, (static_cast<size_t>(m_audioDelayMs) / frameDurationMs) + 50);
				m_audioDelayBuffer = std::make_unique<AudioDelayBuffer>(m_audioDelayMs, capacity);
			std::wstring log(L"--------AudioDelayBuffer created, delayMs=");
			log += to_wstring(m_audioDelayMs);
			OutputDebugStrW(log.c_str());
			}
		}

		while (m_bStartPush) {
			Sleep(1);

			int64_t now = NowMonotonic();

			if (sMemory->isDataReady(AudioData) && sMemory->readAudioDate(m_obsAudio)) {
				if (!sizeLogged) {
					std::wstring sizeLog(L"audio frame size=");
					sizeLog += to_wstring(m_obsAudio.size);
					OutputDebugStrW(sizeLog.c_str());
					sizeLogged = true;
				}

				// 帧大小与声明的 480*2ch*2B 不符时：
				// 超出的无法放入只能丢弃；不足的把尾部补静音（避免把上一帧
				// 残留数据推出去产生杂音），然后照常推送，不打断原有行为。
				if (m_obsAudio.size > kAudioFrameBytes) {
					if (badSizeCount++ % 100 == 0) {
						std::wstring badLog(L"drop audio frame, unexpected size=");
						badLog += to_wstring(m_obsAudio.size);
						OutputDebugStrW(badLog.c_str());
					}
				} else {
					if (m_obsAudio.size < kAudioFrameBytes) {
						memset(m_audioBuffer + m_obsAudio.size, 0, kAudioFrameBytes - m_obsAudio.size);
						if (badSizeCount++ % 100 == 0) {
							std::wstring badLog(L"audio frame short, padded: size=");
							badLog += to_wstring(m_obsAudio.size);
							OutputDebugStrW(badLog.c_str());
						}
					}
					memcpy_s(m_audioBuffer, kAudioFrameBytes, m_obsAudio.buffer, m_obsAudio.size);
					const int64_t time = MapTimestamp(m_obsAudio.timestamp);
					std::lock_guard<std::mutex> lock(m_stateMutex);
					if (m_audioDelayMs > 0 && m_audioDelayBuffer != nullptr) {
						m_audioDelayBuffer->push(m_audioBuffer, kAudioFrameBytes, time, now);
					} else {
						m_audioFrame.renderTimeMs = time + m_audioDelayMs;
						m_audioFrame.buffer = m_audioBuffer;
						int aret = m_mediaEngine->pushAudioFrame(&m_audioFrame, m_audioId);
						if (aret != 0 && audioCount % 100 == 0) {
							std::wstring log(L"---------pushAudioFrame failed, ret=");
							log += to_wstring(aret);
							OutputDebugStrW(log.c_str());
						}
#ifndef LOGTEST
						if (audioCount % 100 == 0)
#endif
						{
							std::wstring log(L"---------m_mediaEngine->pushAudioFrame----");
							log += to_wstring(m_audioFrame.renderTimeMs);
							OutputDebugStrW(log.c_str());
						}
						audioCount++;
					}
				}
			}

			// 延迟队列出队独立于读取流程，任何 continue/失败都不能跳过它。
			std::lock_guard<std::mutex> lock(m_stateMutex);
			if (m_audioDelayMs > 0 && m_audioDelayBuffer != nullptr) {
				AudioFrameItem frame;
				while (m_audioDelayBuffer->pop(frame, now)) {
					memcpy_s(m_audioBuffer, kAudioFrameBytes, frame.buffer.data(), frame.size);
					m_audioFrame.renderTimeMs = frame.renderTimeMs + m_audioDelayMs;
					m_audioFrame.buffer = m_audioBuffer;

					int aret = m_mediaEngine->pushAudioFrame(&m_audioFrame, m_audioId);
					if (aret != 0 && audioCount % 100 == 0) {
						std::wstring log(L"---------pushAudioFrame(delayed) failed, ret=");
						log += to_wstring(aret);
						OutputDebugStrW(log.c_str());
					}
#ifndef LOGTEST
					if (audioCount % 100 == 0)
#endif
					{
						std::wstring log(L"---------m_mediaEngine->pushAudioFrame(delayed)----");
						log += to_wstring(m_audioFrame.renderTimeMs);
						log += L", queueSize=";
						log += to_wstring(m_audioDelayBuffer->size());
						OutputDebugStrW(log.c_str());
					}
					audioCount++;
				}
			}
		}

		std::lock_guard<std::mutex> lock(m_stateMutex);
		if (m_audioDelayBuffer != nullptr) {
			m_audioDelayBuffer->clear();
		}
		} catch (const std::exception& e) {
			OutputDebugStringA(e.what());
			m_bStartPush.store(false);
			// 线程异常退出时也必须通知 OBS 停止写帧，保持事件配对。
			SignalObsStop();
		}
	});
}

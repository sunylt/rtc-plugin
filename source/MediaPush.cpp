
//#define _HAS_BYTE 0
//#include <Windows.h>
//#include <atlbase.h>
#include "MediaPush.h"
#include <thread>
#include <chrono>
#include <mutex>
#include "Tracer.h"


const wchar_t kSharedVideoMemName[] = L"RTCVideoSharedMemory";
const wchar_t kMutexVideoName[] = L"RTCVideoMutex";
const wchar_t kSharedAudioMemName[] = L"RTCAudioSharedMemory";
const wchar_t kMutexAudioName[] = L"RTCAudioMutex";


const wchar_t eventStart[] = L"Start_Push_RTC";
const wchar_t eventStop[] = L"Stop_Push_RTC";
HANDLE g_startPush = nullptr;
HANDLE g_stopPush = nullptr;
int64_t g_offset = -1;
std::mutex g_mtxData;

AudioDelayBuffer::AudioDelayBuffer(int delayMs, size_t maxFrames)
	: m_delayMs(delayMs), m_maxFrames(maxFrames)
{
}

bool AudioDelayBuffer::push(const uint8_t* data, size_t size, int64_t renderTimeMs, int64_t nowMs)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if (m_delayMs <= 0) {
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
	int64_t releaseTime = front.enqueueTimeMs + m_delayMs;

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
	m_delayMs = delayMs;
}

int AudioDelayBuffer::getDelayMs() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_delayMs;
}




int64_t get_time_stamp()
{
	std::chrono::system_clock::duration d = std::chrono::system_clock::now().time_since_epoch();
	std::chrono::milliseconds mic = std::chrono::duration_cast<std::chrono::milliseconds>(d);
	//��ǰʱ���ms ��λ
	return mic.count();
}

int64_t get_timeoffset(IRtcEngine *engine, uint64_t pts)
{
	std::lock_guard<std::mutex> lockSendData(g_mtxData);
	if (g_offset != -1) {
		return g_offset;
	}
	//engine->getCurrentMonotonicTimeInMs() ����gettickcount()ʱ�� ms
	g_offset = engine->getCurrentMonotonicTimeInMs() - pts;
	wchar_t log[MAX_PATH]{0};
	swprintf_s(log, L"GetTickCount  %lld,,get_timeoffset  %lld,,pts==%llu", 
		engine->getCurrentMonotonicTimeInMs(), g_offset, pts);
	OutputDebugStrW(log);
	//blog(LOG_INFO, "GetTickCount()  %llu,,get_timeoffset  %llu,,pts==%lld ", GetTickCount, g_offset, pts);
	return g_offset;
}

MediaPushEngine::MediaPushEngine(agora::rtc::IRtcEngine *rtc_engine,
                                 track_id_t audioId, int colorSpace, int audioDelayMs)
    : m_rtcEngine(rtc_engine),
      m_audioId(audioId),
	  m_obsColorSpace(colorSpace),
	  m_audioDelayMs(audioDelayMs)
{
    wstring log(L"---------MediaPushEngine----------audioId------");
	log += to_wstring(audioId);
	log += L", delayMs=";
	log += to_wstring(audioDelayMs);
    OutputDebugStrW(log.c_str());
	m_obsVideo = {0};
	m_obsAudio = {0};
    m_bInitialize = true;
}

MediaPushEngine ::~MediaPushEngine()
{
    m_rtcEngine = nullptr;
	if (m_audioDelayBuffer != nullptr) {
		delete m_audioDelayBuffer;
		m_audioDelayBuffer = nullptr;
	}
}


bool MediaPushEngine::Initialize(track_id_t audioId, int colorSpace)
{
	bool ret = false;
	wstring log(L"---------MediaPushEngine----------InitEvent------");
	g_startPush = OpenEvent(EVENT_ALL_ACCESS, false, eventStart);
	if (g_startPush == NULL) {

		log = L"OpenEvent   g_startPush  error";
		log += to_wstring(GetLastError());
		OutputDebugStrW(log.c_str());
		return ret;
	}
	g_stopPush = OpenEvent(EVENT_ALL_ACCESS, false, eventStop);
	if (g_stopPush == NULL) {

		log = L"OpenEvent   g_stopPush  error";
		log += to_wstring(GetLastError());
		OutputDebugStrW(log.c_str());
		return ret;
	}
	SetEvent(g_startPush);
	m_audioId = audioId;
	m_obsColorSpace = colorSpace;
	return true;
}

void MediaPushEngine::SetAudioDelay(int delayMs)
{
	m_audioDelayMs = delayMs;
	if (m_audioDelayBuffer != nullptr) {
		m_audioDelayBuffer->setDelayMs(delayMs);
		m_audioDelayBuffer->clear();
	}
}

int MediaPushEngine::GetAudioDelay() const
{
	return m_audioDelayMs;
}

bool MediaPushEngine::EnablePlugin() 
{
  bool ret = false;
  if (m_rtcEngine) 
  {
      ret = m_rtcEngine->queryInterface(agora::rtc::AGORA_IID_MEDIA_ENGINE,
                                (void **) &m_mediaEngine);
  }

  if (!m_bInitialize) {
	  OutputDebugStrW(L"--------!!!!m_bInitialize---------");
	  return ret;
  }
  if (m_mediaEngine) { 
      OutputDebugStrW(L"--------EnablePlugin---------");
      m_bStartPush = true;
      ReadVideoFromMemory();
      ReadAudioFromMemory();
  }
	return ret == 0;
}

bool MediaPushEngine::DisablePlugin()
{
	//bool ret = false;
    m_bStartPush = false;
    std::thread th([&]{
      Sleep(100);

      SetEvent(g_stopPush);
      if (m_obsVideo.buffer != nullptr) {
        delete[] m_obsVideo.buffer;
        m_obsVideo.buffer = nullptr;
      }

      if (m_imgBuffer != nullptr) {
        delete[] m_imgBuffer;
        m_imgBuffer = nullptr;
      }

      if (m_audioBuffer != nullptr) {
        delete[] m_audioBuffer;
        m_audioBuffer = nullptr;
      }

      if (m_audioDelayBuffer != nullptr) {
        delete m_audioDelayBuffer;
        m_audioDelayBuffer = nullptr;
      }

      m_audioId = 0;
      m_mediaEngine = nullptr;
        });
    th.detach();
	return true;
}

void MediaPushEngine::ReadVideoFromMemory()
{
  if (m_mediaEngine == nullptr) { 
      return;
  }

  std::thread th([&] {
    SharedMemory sMemory(kSharedVideoMemName, VideoData, true);
	OutputDebugStrW(L"--------ReadVideoFromMemory---------");
    m_obsVideo.buffer = new char[3840 * 2160 * 4];
	m_imgBuffer = new unsigned char[3840 * 2160 * 4];
    //agora::media::base::ExternalVideoFrame rtcFrame;
    static int videoCount = 0;
	agora::media::base::ColorSpace colorSpace;

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
      if (sMemory.isDataReady(VideoData)) {
        bool bRet = sMemory.readData(m_obsVideo.buffer, m_obsVideo.size,
                                     m_obsVideo.width, m_obsVideo.height,
                                     m_obsVideo.format, m_obsVideo.timestamp);

        memcpy_s(m_imgBuffer, m_obsVideo.size, m_obsVideo.buffer,
                 m_obsVideo.size);
		int64_t time = get_timeoffset(m_rtcEngine, m_obsVideo.timestamp) + get_time_stamp();
		m_videoFrame.timestamp = time;
	//m_obsVideo.timestamp;
        m_videoFrame.buffer = m_imgBuffer;
        m_videoFrame.stride = m_obsVideo.width;
        m_videoFrame.height = m_obsVideo.height;
        //agora::media::base::VIDEO_PIXEL_BGRA;
        m_videoFrame.format = (agora::media::base::VIDEO_PIXEL_FORMAT) m_obsVideo.format;
        m_videoFrame.type = agora::media::base::ExternalVideoFrame::VIDEO_BUFFER_RAW_DATA;
		m_videoFrame.colorSpace = colorSpace;
	    if (videoCount % 30 == 0) 
        {
		    wstring log(L"---------m_mediaEngine->pushVideoFrame----------timestamp------");
		    log += to_wstring(m_videoFrame.timestamp);
		    OutputDebugStrW(log.c_str());
	    }
	    videoCount++;
        m_mediaEngine->pushVideoFrame(&m_videoFrame /*, this->m_videoId*/);
      }
    }
  });
  th.detach();
}

void MediaPushEngine::ReadAudioFromMemory()
{
	if (m_mediaEngine == nullptr) {
		return;
	}

	std::thread th([&] {
		SharedMemory sMemory(kSharedAudioMemName, AudioData, true);
		OutputDebugStrW(L"--------ReadAudioFromMemory---------");
		//this->m_obsAudio.buffer = new char[480 * 4];
		m_audioBuffer = new unsigned char[480 * 4];
		memset(m_audioBuffer, 0, 480 * 4);
		memset(m_obsAudio.buffer, 0, 480 * 4);
		m_audioFrame.channels = 2;
		m_audioFrame.samplesPerChannel = 480;
		m_audioFrame.samplesPerSec = 48000;
		m_audioFrame.bytesPerSample = TWO_BYTES_PER_SAMPLE;
		m_audioFrame.type = agora::media::IAudioFrameObserver::FRAME_TYPE_PCM16;
		static int audioCount = 0;

		if (m_audioDelayMs > 0 && m_audioDelayBuffer == nullptr) {
			m_audioDelayBuffer = new AudioDelayBuffer(m_audioDelayMs, 500);
			std::wstring log(L"--------AudioDelayBuffer created, delayMs=");
			log += to_wstring(m_audioDelayMs);
			OutputDebugStrW(log.c_str());
		}

		while (m_bStartPush) {
			Sleep(1);

			int64_t now = get_timeoffset(m_rtcEngine, GetTickCount64()) + get_time_stamp();

			if (sMemory.isDataReady(AudioData)) {
				bool bRet = sMemory.readAudioDate(m_obsAudio);

				if (!bRet) {
					continue;
				}

				memcpy_s(m_audioBuffer, 480 * 4, m_obsAudio.buffer, m_obsAudio.size);
				int64_t time = now;

				if (m_audioDelayMs > 0 && m_audioDelayBuffer != nullptr) {
					m_audioDelayBuffer->push(m_audioBuffer, m_obsAudio.size, time, now);
				} else {
					m_audioFrame.renderTimeMs = time + m_audioDelayMs;
					m_audioFrame.buffer = m_audioBuffer;
					m_mediaEngine->pushAudioFrame(&m_audioFrame, m_audioId);
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

			if (m_audioDelayMs > 0 && m_audioDelayBuffer != nullptr) {
				AudioFrameItem frame;
				while (m_audioDelayBuffer->pop(frame, now)) {
					memcpy_s(m_audioBuffer, 480 * 4, frame.buffer.data(), frame.size);
					m_audioFrame.renderTimeMs = frame.renderTimeMs + m_audioDelayMs;
					m_audioFrame.buffer = m_audioBuffer;

					m_mediaEngine->pushAudioFrame(&m_audioFrame, m_audioId);
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
			//
		}

		if (m_audioDelayBuffer != nullptr) {
			m_audioDelayBuffer->clear();
		}
	});
	th.detach();
}
#pragma once
#include <IAgoraMediaEngine.h>
#include <IAgoraRtcEngine.h>
#include <AgoraMediaBase.h>
#include "shared-memory-queue.h"
#include "plugin_base.h"
#include <deque>
#include <vector>
#include <cstdint>
#include <atomic>
#include <memory>
#include <thread>
#include <mutex>
#include <algorithm>

using namespace agora;
using namespace agora::rtc;
using namespace agora::media;

struct AudioFrameItem {
	std::vector<uint8_t> buffer;
	int64_t renderTimeMs = 0;
	int64_t enqueueTimeMs = 0;
	size_t size = 0;
};

class AudioDelayBuffer {
public:
	explicit AudioDelayBuffer(int delayMs, size_t maxFrames = 500);

	bool push(const uint8_t* data, size_t size, int64_t renderTimeMs, int64_t nowMs);
	bool pop(AudioFrameItem& outFrame, int64_t nowMs);
	void clear();
	size_t size() const;
	size_t capacity() const;
	void setDelayMs(int delayMs);
	int getDelayMs() const;

private:
	std::deque<AudioFrameItem> m_queue;
	int m_delayMs = 0;
	size_t m_maxFrames = 500;
	mutable std::mutex m_mutex;
};

struct VideoFrameItem {
	std::vector<uint8_t> pixel;
	int64_t renderTimeMs = 0;
	int64_t enqueueTimeMs = 0;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t stride = 0;
	uint32_t format = 0;
	agora::media::base::ColorSpace colorSpace;
};

class VideoDelayBuffer {
public:
	explicit VideoDelayBuffer(int delayMs, size_t maxFrames = 120);

	bool push(VideoFrameItem&& item, int64_t nowMs);
	bool pop(VideoFrameItem& outFrame, int64_t nowMs);
	void clear();
	size_t size() const;
	size_t capacity() const;
	void setDelayMs(int delayMs);

private:
	std::deque<VideoFrameItem> m_queue;
	int m_delayMs = 0;
	size_t m_maxFrames = 120;
	mutable std::mutex m_mutex;
};

class  MediaPushEngine :
	public IPlugin
{
public:
	MediaPushEngine(agora::rtc::IRtcEngine *rtc_engine, track_id_t audioId, int colorSpace, int audioDelayMs = 0);
	~MediaPushEngine();
	bool EnablePlugin() override;

	bool DisablePlugin() override;

	void ReadVideoFromMemory();
	void ReadAudioFromMemory();

	bool Initialize(track_id_t audioId, int colorSpace);

	void SetAudioDelay(int delayMs);
	int GetAudioDelay() const;
	agora::rtc::IRtcEngine *GetRtcEngine() const { return m_rtcEngine; }

private:
	int64_t NowMonotonic() const;
	// 音频/视频必须各自独立锚定：写入侧时间戳不是同一时钟域
	// （视频为 OBS 纳秒时间戳，音频为 0 或毫秒），共享锚点会把音频映射到
	// 错误的时间域，导致音频延迟缓冲立即释放或永不释放。
	int64_t MapTimestamp(uint64_t sourceTimestamp, bool isVideo);
	void StopThreads();
	// 通过 m_obsStarted 的 exchange 保证每次成功的 start 只对应一次 stop，
	// 正常 Disable、析构或线程异常路径都不会重复发或漏发事件。
	void SignalObsStop();

	agora::rtc::IRtcEngine *m_rtcEngine = nullptr;
	agora::media::IMediaEngine *m_mediaEngine = nullptr;
	rtc_video_format m_obsVideo;
	rtc_audio_format m_obsAudio;
	unsigned char *m_audioBuffer = nullptr;
	video_track_id_t m_videoId = 0;
	track_id_t m_audioId = 0;
	int m_obsColorSpace = 0;
	agora::media::base::ExternalVideoFrame m_videoFrame;
	agora::media::IAudioFrameObserver::AudioFrame m_audioFrame;
	std::atomic<bool> m_bStartPush{false};
	bool m_bInitialize = false;
	int m_requestedDelayMs = 0;
	std::atomic<int> m_audioDelayMs{0};
	std::atomic<int> m_videoDelayMs{0};
	std::unique_ptr<AudioDelayBuffer> m_audioDelayBuffer;
	std::unique_ptr<VideoDelayBuffer> m_videoDelayBuffer;
	std::thread m_videoThread;
	std::thread m_audioThread;
	mutable std::mutex m_stateMutex;
	std::mutex m_videoTsMutex;
	std::mutex m_audioTsMutex;
	int64_t m_videoTsOffset = -1;
	int64_t m_audioTsOffset = -1;
	std::atomic<bool> m_obsStarted{false};
	bool m_timerResolutionSet = false;
};


int64_t get_time_stamp();


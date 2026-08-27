#pragma once
#include <IAgoraMediaEngine.h>
#include <IAgoraRtcEngine.h>
#include <AgoraMediaBase.h>
#include "shared-memory-queue.h"
#include "plugin_base.h"
#include <deque>
#include <vector>
#include <cstdint>

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

private:
	agora::rtc::IRtcEngine *m_rtcEngine = nullptr;
	agora::media::IMediaEngine *m_mediaEngine = nullptr;
	rtc_video_format m_obsVideo;
	rtc_audio_format m_obsAudio;
	unsigned char *m_imgBuffer = nullptr;
	unsigned char *m_audioBuffer = nullptr;
	video_track_id_t m_videoId = 0;
	track_id_t m_audioId = 0;
	int m_obsColorSpace = 0;
	agora::media::base::ExternalVideoFrame m_videoFrame;
	agora::media::IAudioFrameObserver::AudioFrame m_audioFrame;
	bool m_bStartPush = false;
	bool m_bInitialize = false;
	int m_audioDelayMs = 0;
	AudioDelayBuffer* m_audioDelayBuffer = nullptr;
};


int64_t get_time_stamp();
int64_t get_timeoffset(IRtcEngine *engine, uint64_t pts);
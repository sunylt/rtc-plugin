#pragma once
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <string>
#include <atomic>
#include "Tracer.h"
using namespace std;

#ifdef __cplusplus
extern "C" {
#endif

struct video_queue;
struct nv12_scale;
typedef struct video_queue video_queue_t;
typedef struct nv12_scale nv12_scale_t;

enum queue_state {
	SHARED_QUEUE_STATE_INVALID,
	SHARED_QUEUE_STATE_STARTING,
	SHARED_QUEUE_STATE_READY,
	SHARED_QUEUE_STATE_STOPPING,
};

struct rtc_video_format {
	uint32_t width;
	uint32_t height;
	uint32_t size;
	uint32_t format;
	uint64_t timestamp;
	char *buffer;
	//int buffer_size;
	//int buffer_offer;
};

struct rtc_audio_format {
	uint32_t size;
	uint64_t timestamp;
	char buffer[1920];
};

//struct rtc_audio_format {
//	uint32_t size;
//	uint64_t timestamp;
//	char *buffer;
//};

// 共享内存数据结构（包含控制块和数据缓冲区）
#pragma pack(push, 1)
struct SharedMemoryVideoData {
	std::atomic<bool> isReady;   // 数据是否就绪
	std::atomic<bool> isWriting; // 是否正在写入
	uint32_t dataSize;           // 实际数据大小
	uint32_t width;              // 视频宽度
	uint32_t height;             // 视频高度
	uint32_t format;             // 视频格式
	uint64_t timestamp;          // 时间戳
    char videoData[3840 * 2160 * 4];// 数据缓冲区（）
};

struct SharedMemoryAudioData {
	std::atomic<bool> isReady;   // 数据是否就绪
	std::atomic<bool> isWriting; // 是否正在写入
	uint32_t dataSize;           // 实际数据大小
	uint64_t timestamp;          // 时间戳
	char audioData[1920 * 1080 * 4];  // 数据缓冲区（）
};
#pragma pack(pop)

enum DataType 
{ 
	AudioData = 1,
	VideoData 
};

//extern void copy_frame_data_plane2(uint8_t *dst, int line_size, const struct obs_source_frame *src, uint32_t plane, uint32_t lines);
//
//extern void copy_frame_data_line2(uint8_t *dst, int line_size, const struct obs_source_frame *src, uint32_t plane, uint32_t y);


class SharedMemory {
private:
	std::wstring name;      // 共享内存名称
	HANDLE m_hMapFile;        // 共享内存句柄
	SharedMemoryVideoData *m_videoData = nullptr; // 共享内存数据指针
	SharedMemoryAudioData *m_audioData = nullptr;
	HANDLE m_hMutex = nullptr;          // 互斥锁句柄

public:
	// 构造函数（创建新的共享内存）
	explicit SharedMemory(const std::wstring &name, DataType type);

	// 构造函数（打开现有共享内存）
	explicit SharedMemory(const std::wstring &name, DataType type, bool openExisting);

	// 析构函数
	~SharedMemory()
	{
		if (m_videoData) {
			UnmapViewOfFile(m_videoData);
		}
		if (m_audioData) {
			UnmapViewOfFile(m_audioData);
		}
		if (m_hMapFile) {
			CloseHandle(m_hMapFile);
		}
		if (m_hMutex) {
			CloseHandle(m_hMutex);
		}
	}

	// 写入数据
	void writeVideoData(rtc_video_format videoData);
	void writeAudioData(rtc_audio_format audioData);

	// 读取数据
	bool readVideoDate(rtc_video_format &videoData);
	bool readAudioDate(rtc_audio_format &audioData);
	bool readData(char *buffer, uint32_t &size, uint32_t &width, uint32_t &height, uint32_t &format, uint64_t &timestamp)
	{
		// 等待互斥锁
		WaitForSingleObject(m_hMutex, INFINITE);

		// 检查数据是否就绪且未在写入
		if (!m_videoData->isReady || m_videoData->isWriting) {
			ReleaseMutex(m_hMutex);
			return false;
		}

		// 复制数据
		size = m_videoData->dataSize;
		width = m_videoData->width;
		height = m_videoData->height;
		format = m_videoData->format;
		timestamp = m_videoData->timestamp;

		if (buffer && size <= sizeof(m_videoData->videoData)) {
			memcpy(buffer, m_videoData->videoData, size);
		}

		// 标记数据已读取
		m_videoData->isReady = false;

		// 释放互斥锁
		ReleaseMutex(m_hMutex);
		return true;
	}

	// 检查数据是否就绪
	bool isDataReady(DataType type);

};
#ifdef __cplusplus
}
#endif

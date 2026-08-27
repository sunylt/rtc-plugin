#pragma once
#include <windows.h>
#include <atomic>
#include <string>
#include <stdexcept>
#include <stdio.h>
//#include <obs.h>
#include "shared-memory-queue.h"

using namespace std;

SharedMemory::SharedMemory(const std::wstring &name, DataType type) : name(name)
{
	SECURITY_ATTRIBUTES sa;
	SECURITY_DESCRIPTOR sd;
	InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
	SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE);
	sa.nLength = sizeof(sa);
	sa.lpSecurityDescriptor = &sd;
	sa.bInheritHandle = FALSE;

	// 创建共享内存
	m_hMapFile = CreateFileMapping(INVALID_HANDLE_VALUE, 
			&sa, PAGE_READWRITE, 0, sizeof(SharedMemoryVideoData), 
		std::wstring(name.begin(), name.end()).c_str());

	if (m_hMapFile == NULL) 
	{
		throw std::runtime_error("创建共享内存失败");
	}

	// 映射内存
	if (type == VideoData) 
	{
		m_videoData = (SharedMemoryVideoData *)MapViewOfFile(m_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedMemoryVideoData));

		if (m_videoData == NULL) 
		{
			CloseHandle(m_hMapFile);
			throw std::runtime_error("映射共享内存失败");
		}

		// 创建互斥锁
		m_hMutex = CreateMutex(NULL, FALSE, std::wstring(name + L"_mutex").c_str());

		if (m_hMutex == NULL) {
			UnmapViewOfFile(m_videoData);
			CloseHandle(m_hMapFile);
			throw std::runtime_error("创建互斥锁失败");
		}

		// 初始化数据
		m_videoData->isReady = false;
		m_videoData->isWriting = false;
		m_videoData->dataSize = 0;
	}
	else if (type == AudioData) 
	{
		m_audioData = (SharedMemoryAudioData *)MapViewOfFile(m_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedMemoryAudioData));

		if (m_audioData == NULL) {
			CloseHandle(m_hMapFile);
			throw std::runtime_error("映射共享内存失败");
		}

		// 创建互斥锁
		m_hMutex = CreateMutex(NULL, FALSE, std::wstring(name + L"_mutex").c_str());

		if (m_hMutex == NULL) {
			UnmapViewOfFile(m_audioData);
			CloseHandle(m_hMapFile);
			throw std::runtime_error("创建互斥锁失败");
		}

		// 初始化数据
		m_audioData->isReady = false;
		m_audioData->isWriting = false;
		m_audioData->dataSize = 0;
	}
}

SharedMemory::SharedMemory(const std::wstring &name, DataType type, bool openExisting) : name(name) 
{
	// 打开现有共享内存
	m_hMapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, std::wstring(name.begin(), name.end()).c_str());

	if (m_hMapFile == NULL) {
		throw std::runtime_error("打开共享内存失败");
	}

	if (type == VideoData) {
		// 映射内存
		m_videoData = (SharedMemoryVideoData *)MapViewOfFile(m_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedMemoryVideoData));

		if (m_videoData == NULL) {
			CloseHandle(m_hMapFile);
			throw std::runtime_error("映射共享内存失败");
		}

		// 打开互斥锁
		m_hMutex = OpenMutex(MUTEX_ALL_ACCESS, FALSE, std::wstring(name + L"_mutex").c_str());

		if (m_hMutex == NULL) {
			UnmapViewOfFile(m_videoData);
			CloseHandle(m_hMapFile);
			throw std::runtime_error("打开互斥锁失败");
		}
	}
	else if (type == AudioData)
	{
		// 映射内存
		m_audioData = (SharedMemoryAudioData *)MapViewOfFile(m_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedMemoryAudioData));

		if (m_audioData == NULL) {
			CloseHandle(m_hMapFile);
			throw std::runtime_error("映射共享内存失败");
		}

		// 打开互斥锁
		m_hMutex = OpenMutex(MUTEX_ALL_ACCESS, FALSE, std::wstring(name + L"_mutex").c_str());

		if (m_hMutex == NULL) {
			UnmapViewOfFile(m_audioData);
			CloseHandle(m_hMapFile);
			throw std::runtime_error("打开互斥锁失败");
		}
	}
}

void SharedMemory::writeVideoData(rtc_video_format videoData)
{
	// 等待互斥锁
	WaitForSingleObject(m_hMutex, INFINITE);

	// 标记正在写入
	m_videoData->isWriting = true;

	// 复制数据
	if (videoData.size <= sizeof(m_videoData->videoData)) {
		memcpy(m_videoData->videoData, videoData.buffer, videoData.size);
		m_videoData->dataSize = videoData.size;
		m_videoData->width = videoData.width;
		m_videoData->height = videoData.height;
		m_videoData->format = videoData.format;
		m_videoData->timestamp = videoData.timestamp;
	} else {
		throw std::runtime_error("数据大小超过缓冲区限制");
	}

	// 标记数据就绪
	m_videoData->isReady = true;
	m_videoData->isWriting = false;

	// 释放互斥锁
	ReleaseMutex(m_hMutex);
}

void SharedMemory::writeAudioData(rtc_audio_format audioData)
{
	// 等待互斥锁
	WaitForSingleObject(m_hMutex, INFINITE);

	// 标记正在写入
	m_audioData->isWriting = true;

	// 复制数据
	if (audioData.size <= sizeof(m_audioData->audioData)) {
		memcpy(m_audioData->audioData, audioData.buffer, audioData.size);
		m_audioData->dataSize = audioData.size;
		m_audioData->timestamp = audioData.timestamp;
	} else {
		OutputDebugStrW(L"数据大小超过缓冲区限制");
		throw std::runtime_error("数据大小超过缓冲区限制");
	}

	// 标记数据就绪
	m_audioData->isReady = true;
	m_audioData->isWriting = false;

	// 释放互斥锁
	ReleaseMutex(m_hMutex);
}

bool SharedMemory::isDataReady(DataType type)
{
	if (type == VideoData) 
	{
		return m_videoData->isReady && !m_videoData->isWriting;
	} 
	else if (type == AudioData)
	{
		return m_audioData->isReady && !m_audioData->isWriting;
	}
	else
	{
		return false;
	}
}


bool SharedMemory::readVideoDate(rtc_video_format& videoData)
{
	// 等待互斥锁
	WaitForSingleObject(m_hMutex, INFINITE);

	// 检查数据是否就绪且未在写入
	if (!m_videoData->isReady || m_videoData->isWriting) {
		ReleaseMutex(m_hMutex);
		return false;
	}

	// 复制数据
	videoData.size = m_videoData->dataSize;
	videoData.width = m_videoData->width;
	videoData.height = m_videoData->height;
	videoData.format = m_videoData->format;
	videoData.timestamp = m_videoData->timestamp;

	if (videoData.buffer && videoData.size <= sizeof(m_videoData->videoData)) {
		memcpy(videoData.buffer, m_videoData->videoData, videoData.size);
	}

	// 标记数据已读取
	m_videoData->isReady = false;

	// 释放互斥锁
	ReleaseMutex(m_hMutex);
	return true;
}

bool SharedMemory::readAudioDate(rtc_audio_format& audioData)
{
	// 等待互斥锁
	WaitForSingleObject(m_hMutex, INFINITE);

	// 检查数据是否就绪且未在写入
	if (!m_audioData->isReady || m_audioData->isWriting) {
		OutputDebugStrW(L"检查数据是否就绪且未在写入");
		ReleaseMutex(m_hMutex);
		return false;
	}

	// 复制数据
	audioData.size = m_audioData->dataSize;
	audioData.timestamp = m_audioData->timestamp;

	if (audioData.buffer && audioData.size <= sizeof(m_audioData->audioData)) 
	{
		memcpy(audioData.buffer, m_audioData->audioData, audioData.size);
	}

	// 标记数据已读取
	m_audioData->isReady = false;

	// 释放互斥锁
	ReleaseMutex(m_hMutex);
	return true;
}
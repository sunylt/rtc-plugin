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

	// ���������ڴ�
	SetLastError(ERROR_SUCCESS);
	m_hMapFile = CreateFileMapping(INVALID_HANDLE_VALUE, 
			&sa, PAGE_READWRITE, 0,
			type == VideoData ? sizeof(SharedMemoryVideoData) : sizeof(SharedMemoryAudioData),
		std::wstring(name.begin(), name.end()).c_str());
	const bool isNewMapping = GetLastError() != ERROR_ALREADY_EXISTS;

	if (m_hMapFile == NULL) 
	{
		throw std::runtime_error("���������ڴ�ʧ��");
	}

	// ӳ���ڴ�
	if (type == VideoData) 
	{
		m_videoData = (SharedMemoryVideoData *)MapViewOfFile(m_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedMemoryVideoData));

		if (m_videoData == NULL) 
		{
			CloseHandle(m_hMapFile);
			throw std::runtime_error("ӳ�乲���ڴ�ʧ��");
		}

		// ����������
		m_hMutex = CreateMutex(NULL, FALSE, std::wstring(name + L"_mutex").c_str());

		if (m_hMutex == NULL) {
			UnmapViewOfFile(m_videoData);
			CloseHandle(m_hMapFile);
			throw std::runtime_error("����������ʧ��");
		}

		if (isNewMapping) {
			m_videoData->isReady = false;
			m_videoData->isWriting = false;
			m_videoData->dataSize = 0;
		}
	}
	else if (type == AudioData) 
	{
		m_audioData = (SharedMemoryAudioData *)MapViewOfFile(m_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedMemoryAudioData));

		if (m_audioData == NULL) {
			CloseHandle(m_hMapFile);
			throw std::runtime_error("ӳ�乲���ڴ�ʧ��");
		}

		// ����������
		m_hMutex = CreateMutex(NULL, FALSE, std::wstring(name + L"_mutex").c_str());

		if (m_hMutex == NULL) {
			UnmapViewOfFile(m_audioData);
			CloseHandle(m_hMapFile);
			throw std::runtime_error("����������ʧ��");
		}

		if (isNewMapping) {
			m_audioData->isReady = false;
			m_audioData->isWriting = false;
			m_audioData->dataSize = 0;
		}
	}
}

SharedMemory::SharedMemory(const std::wstring &name, DataType type, bool openExisting) : name(name) 
{
	// �����й����ڴ�
	m_hMapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, std::wstring(name.begin(), name.end()).c_str());

	if (m_hMapFile == NULL) {
		throw std::runtime_error("�򿪹����ڴ�ʧ��");
	}

	if (type == VideoData) {
		// ӳ���ڴ�
		m_videoData = (SharedMemoryVideoData *)MapViewOfFile(m_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedMemoryVideoData));

		if (m_videoData == NULL) {
			CloseHandle(m_hMapFile);
			throw std::runtime_error("ӳ�乲���ڴ�ʧ��");
		}

		// �򿪻�����
		m_hMutex = OpenMutex(MUTEX_ALL_ACCESS, FALSE, std::wstring(name + L"_mutex").c_str());

		if (m_hMutex == NULL) {
			UnmapViewOfFile(m_videoData);
			CloseHandle(m_hMapFile);
			throw std::runtime_error("�򿪻�����ʧ��");
		}
	}
	else if (type == AudioData)
	{
		// ӳ���ڴ�
		m_audioData = (SharedMemoryAudioData *)MapViewOfFile(m_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedMemoryAudioData));

		if (m_audioData == NULL) {
			CloseHandle(m_hMapFile);
			throw std::runtime_error("ӳ�乲���ڴ�ʧ��");
		}

		// �򿪻�����
		m_hMutex = OpenMutex(MUTEX_ALL_ACCESS, FALSE, std::wstring(name + L"_mutex").c_str());

		if (m_hMutex == NULL) {
			UnmapViewOfFile(m_audioData);
			CloseHandle(m_hMapFile);
			throw std::runtime_error("�򿪻�����ʧ��");
		}
	}
}

void SharedMemory::writeVideoData(rtc_video_format videoData)
{
	if (m_videoData == nullptr || m_hMutex == nullptr || videoData.buffer == nullptr) return;
	if (videoData.size > sizeof(m_videoData->videoData)) {
		OutputDebugStrW(L"Video data exceeds shared memory capacity");
		return;
	}
	// �ȴ�������
	const DWORD waitResult = WaitForSingleObject(m_hMutex, INFINITE);
	if (waitResult != WAIT_OBJECT_0 && waitResult != WAIT_ABANDONED) return;

	// �������д��
	m_videoData->isWriting = true;
	MemoryBarrier();

	// ��������
	memcpy(m_videoData->videoData, videoData.buffer, videoData.size);
	m_videoData->dataSize = videoData.size;
	m_videoData->width = videoData.width;
	m_videoData->height = videoData.height;
	m_videoData->format = videoData.format;
	m_videoData->timestamp = videoData.timestamp;

	// ������ݾ���
	m_videoData->isReady = true;
	MemoryBarrier();
	m_videoData->isWriting = false;
	MemoryBarrier();

	// �ͷŻ�����
	ReleaseMutex(m_hMutex);
}

void SharedMemory::writeAudioData(rtc_audio_format audioData)
{
	// 源缓冲区只有 1920 字节（rtc_audio_format::buffer），
	// 校验上限不能用共享内存里的大数组，否则越界读源数据。
	if (m_audioData == nullptr || m_hMutex == nullptr || audioData.size > sizeof(audioData.buffer)) return;
	// �ȴ�������
	const DWORD waitResult = WaitForSingleObject(m_hMutex, INFINITE);
	if (waitResult != WAIT_OBJECT_0 && waitResult != WAIT_ABANDONED) return;

	// �������д��
	m_audioData->isWriting = true;
	MemoryBarrier();

	// ��������
	if (audioData.size > 0 && audioData.buffer == nullptr) {
		m_audioData->isWriting = false;
		ReleaseMutex(m_hMutex);
		return;
	}
	memcpy(m_audioData->audioData, audioData.buffer, audioData.size);
	m_audioData->dataSize = audioData.size;
	m_audioData->timestamp = audioData.timestamp;

	// ������ݾ���
	m_audioData->isReady = true;
	MemoryBarrier();
	m_audioData->isWriting = false;
	MemoryBarrier();

	// �ͷŻ�����
	ReleaseMutex(m_hMutex);
}

bool SharedMemory::isDataReady(DataType type)
{
	if (type == VideoData) 
	{
		bool ready = m_videoData != nullptr && m_videoData->isReady;
		MemoryBarrier();
		return ready && !m_videoData->isWriting;
	} 
	else if (type == AudioData)
	{
		bool ready = m_audioData != nullptr && m_audioData->isReady;
		MemoryBarrier();
		return ready && !m_audioData->isWriting;
	}
	else
	{
		return false;
	}
}


bool SharedMemory::readVideoDate(rtc_video_format& videoData)
{
	if (m_videoData == nullptr || m_hMutex == nullptr || videoData.buffer == nullptr) return false;
	// �ȴ�������
	const DWORD waitResult = WaitForSingleObject(m_hMutex, INFINITE);
	if (waitResult != WAIT_OBJECT_0 && waitResult != WAIT_ABANDONED) return false;

	// ��������Ƿ������δ��д��
	bool ready = m_videoData->isReady;
	MemoryBarrier();
	bool writing = m_videoData->isWriting;
	if (!ready || writing) {
		ReleaseMutex(m_hMutex);
		return false;
	}

	// ��������
	videoData.size = m_videoData->dataSize;
	videoData.width = m_videoData->width;
	videoData.height = m_videoData->height;
	videoData.format = m_videoData->format;
	videoData.timestamp = m_videoData->timestamp;
	if (videoData.size > sizeof(m_videoData->videoData)) {
		m_videoData->isReady = false;
		MemoryBarrier();
		ReleaseMutex(m_hMutex);
		return false;
	}

	if (videoData.buffer && videoData.size <= sizeof(m_videoData->videoData)) {
		memcpy(videoData.buffer, m_videoData->videoData, videoData.size);
	}

	// ��������Ѷ�ȡ
	m_videoData->isReady = false;
	MemoryBarrier();

	// �ͷŻ�����
	ReleaseMutex(m_hMutex);
	return true;
}

bool SharedMemory::readAudioDate(rtc_audio_format& audioData)
{
	if (m_audioData == nullptr || m_hMutex == nullptr) return false;
	// �ȴ�������
	const DWORD waitResult = WaitForSingleObject(m_hMutex, INFINITE);
	if (waitResult != WAIT_OBJECT_0 && waitResult != WAIT_ABANDONED) return false;

	// ��������Ƿ������δ��д��
	bool ready = m_audioData->isReady;
	MemoryBarrier();
	bool writing = m_audioData->isWriting;
	if (!ready || writing) {
		ReleaseMutex(m_hMutex);
		return false;
	}

	// ��������
	audioData.size = m_audioData->dataSize;
	audioData.timestamp = m_audioData->timestamp;
	// 注意：校验上限必须是目标缓冲区（rtc_audio_format::buffer，1920 字节），
	// 不能用共享内存里的大数组，否则 dataSize 异常时会越界写坏内存。
	if (audioData.size > sizeof(audioData.buffer)) {
		m_audioData->isReady = false;
		MemoryBarrier();
		ReleaseMutex(m_hMutex);
		return false;
	}

	memcpy(audioData.buffer, m_audioData->audioData, audioData.size);

	// ��������Ѷ�ȡ
	m_audioData->isReady = false;
	MemoryBarrier();

	// �ͷŻ�����
	ReleaseMutex(m_hMutex);
	return true;
}

#pragma once
#include "ipc-client.hpp"
#include "ipc-server.hpp"
#include <iostream>
#include <mutex>

#include <functional>
#include <inttypes.h>
#include <list>
#include <map>
#include <math.h>
#include <napi.h>
#include <uv.h>
#include <vector>
#include <AgoraBase.h>
#include <IAgoraRtcEngine.h>
#include <AgoraMediaBase.h>
#include "shared-memory-queue.h"
#include "plugin_base.h"
#include "MediaPush.h"

using namespace std;
using namespace agora;
using namespace agora::rtc;
using namespace agora::media;

#ifdef MYDLL_EXPORTS
#define MYDLL_API __declspec(dllexport)
#else
#define MYDLL_API __declspec(dllimport)
#endif

//extern "C" {
//MYDLL_API int add(int a, int b);
//MYDLL_API const char *getMessage();
//MYDLL_API void setMessage(const char *message);
//}


class MediaPushEngine;
static MediaPushEngine *g_rtc_media_client = NULL;

namespace RawDataApi 
{
	void Init(Napi::Env env, Napi::Object exports);
	Napi::Value RawDataPluginInit(const Napi::CallbackInfo &info);
	Napi::Value EnablePlugin(const Napi::CallbackInfo &info);
	Napi::Value DisablePlugin(const Napi::CallbackInfo &info);
	Napi::Value SetAudioDelay(const Napi::CallbackInfo &info);
	Napi::Value GetAudioDelay(const Napi::CallbackInfo &info);
}

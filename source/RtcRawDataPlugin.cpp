#include "RtcRawDataPlugin.hpp"
#include "Tracer.h"

void RawDataApi::Init(Napi::Env env, Napi::Object exports)
{
	OutputDebugStrW(L"----api::Init------RawDataApi-------API\n");
	exports.Set(Napi::String::New(env, "RawDataPluginInit"), Napi::Function::New(env, RawDataApi::RawDataPluginInit));
	exports.Set(Napi::String::New(env, "EnablePlugin"), Napi::Function::New(env, RawDataApi::EnablePlugin));
	exports.Set(Napi::String::New(env, "DisablePlugin"), Napi::Function::New(env, RawDataApi::DisablePlugin));
	OutputDebugStrW(L"----api::Init   end  API\n");
}

Napi::Value RawDataApi::RawDataPluginInit(const Napi::CallbackInfo &info)
{
	//std::vector<ipc::value> response = conn->call_synchronous_helper(
	//	"API", "OBS_API_initAPI", {ipc::value(path), ipc::value(language), ipc::value(version), ipc::value(crashserverurl)});
	OutputDebugStrW(L"RawDataApi\n");
	OutputDebugStrW(L"11111111111--------IPC---------1111111\n");
	int64_t handle = info[0].As<Napi::Number>().Int64Value();
	int audioId = info[1].As<Napi::Number>().Int32Value();
	int colorSpace = info[2].As<Napi::Number>().Int32Value();
	agora::rtc::IRtcEngine *rtc_engine = (agora::rtc::IRtcEngine *) handle;
	if (rtc_engine == nullptr) {
		OutputDebugStrW(L"--------rtc_engine == nullptr---------\n");
		return Napi::Number::New(info.Env(), false);
	}
	if (g_rtc_media_client == NULL) 
	{
		g_rtc_media_client = new MediaPushEngine(rtc_engine, audioId, colorSpace);
		g_rtc_media_client->Initialize(audioId, colorSpace);
		//return Napi::Number::New(info.Env(), true);
	} 
	else 
	{
		g_rtc_media_client->Initialize(audioId, colorSpace);
		//return Napi::Number::New(info.Env(), true);
	}

	return Napi::Number::New(info.Env(), true);
}

Napi::Value RawDataApi::EnablePlugin(const Napi::CallbackInfo& info)
{
	bool re = false;
	re = g_rtc_media_client->EnablePlugin();
	return Napi::Number::New(info.Env(), re);
}

Napi::Value RawDataApi::DisablePlugin(const Napi::CallbackInfo &info)
{
	bool re = false;
	re = g_rtc_media_client->DisablePlugin();
	return Napi::Number::New(info.Env(), re);
}
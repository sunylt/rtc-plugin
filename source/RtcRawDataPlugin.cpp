#include "RtcRawDataPlugin.hpp"
#include "Tracer.h"

void RawDataApi::Init(Napi::Env env, Napi::Object exports)
{
	OutputDebugStrW(L"----api::Init------RawDataApi-------API\n");
	exports.Set(Napi::String::New(env, "RawDataPluginInit"), Napi::Function::New(env, RawDataApi::RawDataPluginInit));
	exports.Set(Napi::String::New(env, "EnablePlugin"), Napi::Function::New(env, RawDataApi::EnablePlugin));
	exports.Set(Napi::String::New(env, "DisablePlugin"), Napi::Function::New(env, RawDataApi::DisablePlugin));
	exports.Set(Napi::String::New(env, "SetAudioDelay"), Napi::Function::New(env, RawDataApi::SetAudioDelay));
	exports.Set(Napi::String::New(env, "GetAudioDelay"), Napi::Function::New(env, RawDataApi::GetAudioDelay));
	OutputDebugStrW(L"----api::Init   end  API\n");
}

Napi::Value RawDataApi::RawDataPluginInit(const Napi::CallbackInfo &info)
{
	if (info.Length() < 3 || !info[0].IsNumber() || !info[1].IsNumber() || !info[2].IsNumber()) {
		return Napi::Boolean::New(info.Env(), false);
	}
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
	// If a different engine handle is passed, the old engine may have been
	// destroyed; recreate the plugin object to avoid a dangling pointer.
	if (g_rtc_media_client != NULL && g_rtc_media_client->GetRtcEngine() != rtc_engine)
	{
		delete g_rtc_media_client;
		g_rtc_media_client = NULL;
	}

	if (g_rtc_media_client == NULL) 
	{
		g_rtc_media_client = new MediaPushEngine(rtc_engine, audioId, colorSpace);
	}

	if (!g_rtc_media_client->Initialize(audioId, colorSpace)) {
		delete g_rtc_media_client;
		g_rtc_media_client = nullptr;
		return Napi::Boolean::New(info.Env(), false);
	}

	return Napi::Number::New(info.Env(), true);
}

Napi::Value RawDataApi::EnablePlugin(const Napi::CallbackInfo& info)
{
	bool re = false;
	if (g_rtc_media_client != nullptr) {
		re = g_rtc_media_client->EnablePlugin();
	}
	return Napi::Number::New(info.Env(), re);
}

Napi::Value RawDataApi::DisablePlugin(const Napi::CallbackInfo &info)
{
	bool re = false;
	if (g_rtc_media_client != nullptr) {
		re = g_rtc_media_client->DisablePlugin();
	}
	return Napi::Number::New(info.Env(), re);
}

Napi::Value RawDataApi::SetAudioDelay(const Napi::CallbackInfo &info)
{
	if (g_rtc_media_client == nullptr || info.Length() < 1 || !info[0].IsNumber()) {
		return Napi::Boolean::New(info.Env(), false);
	}

	g_rtc_media_client->SetAudioDelay(info[0].As<Napi::Number>().Int32Value());
	return Napi::Boolean::New(info.Env(), true);
}

Napi::Value RawDataApi::GetAudioDelay(const Napi::CallbackInfo &info)
{
	if (g_rtc_media_client == nullptr) {
		return Napi::Number::New(info.Env(), 0);
	}

	return Napi::Number::New(info.Env(), g_rtc_media_client->GetAudioDelay());
}

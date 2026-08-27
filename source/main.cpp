// rtc-plugin.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "RtcRawDataPlugin.hpp"
#include "Tracer.h"

int main(int, char **, char **) {}

Napi::Object main_node(Napi::Env env, Napi::Object exports)
{
	OutputDebugStrW(L"--------Napi::Object main_node-------");
	RawDataApi::Init(env, exports);
	return exports;
};

NODE_API_MODULE(obs_studio_node, main_node);
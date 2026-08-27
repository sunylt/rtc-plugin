//#include "pch.h"
#define _HAS_STD_BYTE 0
#define _HAS_BYTE 0
#include "Tracer.h"
#include "shlobj.h"
#include <comdef.h>

Tracer oTracer;			// definition of global tracer

Tracer::Tracer(void)
{
	OpenNewLogfile();
}

void Tracer::OpenNewLogfile()
{
	way = ReadOutputConfig();
	// get current process id
	DWORD pid = ::GetCurrentProcessId();
	WCHAR   szDocument[MAX_PATH] = { 0 };
	WCHAR   folderPath[MAX_PATH] = { 0 };
	LPITEMIDLIST pidl = NULL;
	SHGetSpecialFolderLocation(NULL, CSIDL_PERSONAL, &pidl);
	SHGetPathFromIDList(pidl, szDocument);
	date = GetCurrentDate();
	wsprintfW(folderPath, L"%s\\Log", szDocument);
	bool flag = CreateDirectory(folderPath, NULL);

	// generate log file name
	WCHAR name[MAX_PATH];
	memset(name, 0, sizeof(name[0])*MAX_PATH);
	wsprintfW(name, L"%s\\Log_%s_PID_%d_0X%X.txt", folderPath, date.c_str(), pid, pid);


#ifdef _DEBUG
	// open log file belonging to current process
	Logger.open(name, ios::binary | ios::out | ios::app);
#endif

#if USERTEST
	// open log file belonging to current process
	Logger.open(name, ios::binary | ios::out | ios::app);
#endif
}
wstring Tracer::GetCurrentDate()
{
	time_t currentTime;
	time(&currentTime);
	struct tm p;
	WCHAR str[30] = { 0 };
    localtime_s(&p, &currentTime);
	swprintf_s(str, 30, L"%d_%02d_%02d", p.tm_year+1900, p.tm_mon + 1, p.tm_mday);
	return str;
}
bool Tracer::IsTracerOpened()
{
	bool bret = false;
	
	if(Logger.is_open()){
		bret = true;
	}

	return bret;
}

void Tracer::WideCharOutput(const WCHAR* ws)
{
	if (ws!= NULL) {
		_bstr_t b(ws);
		CharOutput(b);
	}

}
void Tracer::CharOutput(const char* cs)
{
	wstring curDate = GetCurrentDate();
	if (curDate != date)
	{
		Release();
		OpenNewLogfile();
	}

	if (cs!= NULL && strlen(cs) && oTracer.Logger.is_open()) {
		if (way == 1)
		{
#undef OutputDebugStringA
#undef OutputDebugStringW
			OutputDebugStringA(cs);
		}
		else
		{
#ifndef OutputDebugStringA
	#define OutputDebugStringA TRACE
#endif // OutputDebugStringA

#ifndef OutputDebugStringW
	#define OutputDebugStringW TRACE_W
#endif // OutputDebugStringW
			time_t currentTime;
			time(&currentTime);
			struct tm p;
			char str[20] = { 0 };
            localtime_s(&p, &currentTime);
			
			sprintf_s(str, "%02d:%02d:%02d", p.tm_hour, p.tm_min, p.tm_sec);

			oTracer.Logger << str << "\t" << cs << "\r\n";
			oTracer.Logger.flush();
		}
	};
}

// 1: debugview others: file
int Tracer::ReadOutputConfig()
{
	wchar_t iniPath[MAX_PATH] = { 0 };
	HMODULE hMod = GetModuleHandleW(L"WechatHookApi");
	if (hMod == nullptr)
	{
		return 0;
	}
	GetModuleFileName(hMod, iniPath, MAX_PATH);
	wstring strModPath = iniPath;
	strModPath = strModPath.substr(0, strModPath.rfind(L"\\"));
	wstring strPath = strModPath;
	strPath += L"\\Mqtt.ini";
	wchar_t srtApp[] = L"Debug";
	wchar_t strKey[] = L"Output";

	int way = GetPrivateProfileInt(srtApp, strKey, 0, strPath.c_str());
	/*wchar_t szLog[0x20] = { 0 };
	wsprintf(szLog, L"OutputConfig is %d", way);
	OutputDebugStringW(szLog);*/
	return way;
}

string Tracer::GetTracerFileName()
{
	string sret = "";

	if(IsTracerOpened()){
		sret = sTracerFileName;
	}

	return sret;
}

string Tracer::GetRoutineCallStackIndent(int index)
{
	string sIndent = "";
	string sUnit = " ";

	if(index > 0){
		for(int count=0; count<index; ++count){
			sIndent += sUnit;
		}
	}else{
		sIndent = "";
	}

	return sIndent + ">>>";
}

void Tracer::Release()
{
	// close log file
	if (Logger.is_open()) {
		Logger.close();
	}
}

Tracer::~Tracer(void)
{
#ifdef _DEBUG
	// close log file
	if(Logger.is_open()){
		Logger.close();
	}
#endif

#if USERTEST
	// close log file
	if(Logger.is_open()){
		Logger.close();
	}
#endif

}

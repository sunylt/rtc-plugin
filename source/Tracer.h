#pragma once

#include <string>
#include <fstream>
#include <Windows.h>

using namespace std;

//#define TEST
#ifndef TEST
#define OutputDebugStr TRACE
#define OutputDebugStrW TRACE_W
#endif
#define USERTEST	1


#ifdef _DEBUG
#include <iostream>
#define TRACE(x)	oTracer.CharOutput(x);
#define TRACE_W(x)	oTracer.WideCharOutput(x);
#elif	USERTEST
#define TRACE(x)	oTracer.CharOutput(x);
#define TRACE_W(x)	oTracer.WideCharOutput(x);
#else
#define TRACE(x)	; 
#define TRACE_W(x)	;
#endif

class Tracer
{
public:
	Tracer(void);
	virtual ~Tracer(void);
	static string GetRoutineCallStackIndent(int);
	bool IsTracerOpened();
	void WideCharOutput(const WCHAR* ws);
	void CharOutput(const char* cs);
	int ReadOutputConfig();
	string GetTracerFileName();
	void Release();

	ofstream Logger;
	
private:
	wstring GetCurrentDate();
	void OpenNewLogfile();
		
	char sTracerFileName[MAX_PATH];
	int way;
	wstring date;
};

extern Tracer oTracer;		// declaration of global tracer object

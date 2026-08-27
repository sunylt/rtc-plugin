#pragma once

#include "plugin_base.h"

typedef void *PluginPtr;

bool EnablePlugin(PluginPtr plugin);

bool DisablePlugin(PluginPtr plugin);

PluginPtr CreateMediaPlugin(void *rtcEnginePtr, uint32_t audioTackId, int colorSpace);

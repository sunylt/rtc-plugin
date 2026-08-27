#include "plugin_c_api.h"
#include "MediaPush.h"

bool EnablePlugin(PluginPtr plugin) {
  if (plugin) {
    return ((IPlugin *) plugin)->EnablePlugin();
  } else {
    return false;
  }
}

bool DisablePlugin(PluginPtr plugin) {
  if (plugin) {
    return ((IPlugin *) plugin)->DisablePlugin();
  } else {
    return false;
  }
}

PluginPtr CreateMediaPlugin(void *rtcEnginePtr, uint32_t audioTackId, int colorSpace) {
  auto *plugin =
      new MediaPushEngine((agora::rtc::IRtcEngine *) rtcEnginePtr, audioTackId, colorSpace);
  return (IPlugin *) plugin;
}

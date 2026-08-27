#pragma once

#include <stdint.h>
#include <windows.h>
#include <atomic>
#include <string>
#include <stdexcept>
#include <stdio.h>


class IPlugin {
 public:
  virtual ~IPlugin() = default;

  virtual bool EnablePlugin() = 0;

  virtual bool DisablePlugin() = 0;
};

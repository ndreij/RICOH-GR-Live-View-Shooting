#pragma once

#include <Arduino.h>

#include "camera_profile_store.h"
#include "gr_api.h"

class GrShutterService {
public:
  bool shoot(GrApi& api, const ShutterCommandConfig& config, String& message);
};
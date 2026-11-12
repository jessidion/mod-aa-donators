cmake_minimum_required(VERSION 3.21)
project(mod_aa_donators LANGUAGES CXX)

if(NOT TARGET CoA::PluginSDK)
  if(NOT DEFINED COA_PLUGIN_SDK_DIR)
    message(FATAL_ERROR "Set -DCOA_PLUGIN_SDK_DIR=<full path to .manager/sdk/<fingerprint>>")
  endif()
  include("${COA_PLUGIN_SDK_DIR}/cmake/CoAPluginSDKConfig.cmake")
endif()

coa_add_plugin(mod_aa_donators src/AA_DonatorScript.cpp standalone/adapter.cpp)
set_target_properties(mod_aa_donators PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES)

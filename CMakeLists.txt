# ESP-IDF project for the desk companion (M5Stack CoreS3).
#   idf.py set-target esp32s3   (first time only)
#   idf.py build flash monitor
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(desk_companion)

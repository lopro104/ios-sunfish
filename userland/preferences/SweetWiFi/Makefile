ARCHS = arm64 arm64e
TARGET := iphone:clang:latest:14.0
THEOS_PACKAGE_SCHEME = rootless

include $(THEOS)/makefiles/common.mk

BUNDLE_NAME = SweetWiFi

SweetWiFi_FILES = SweetWiFiRootListController.m
SweetWiFi_FRAMEWORKS = UIKit CoreGraphics
SweetWiFi_PRIVATE_FRAMEWORKS = Preferences
SweetWiFi_INSTALL_PATH = /Library/PreferenceBundles
SweetWiFi_CFLAGS = -fobjc-arc

include $(THEOS_MAKE_PATH)/bundle.mk

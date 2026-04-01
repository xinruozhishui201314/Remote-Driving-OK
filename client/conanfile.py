from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMake, cmake_layout


class RemoteDrivingClientConan(ConanFile):
    name = "remote-driving-client"
    version = "1.0.0"
    package_type = "application"
    description = "Remote Driving Client - Qt6/C++20 four-layer architecture"
    license = "Proprietary"
    settings = "os", "compiler", "build_type", "arch"

    options = {
        "with_vaapi":   [True, False],
        "with_ffmpeg":  [True, False],
        "with_webrtc":  [True, False],
        "with_mqtt":    [True, False],
    }
    default_options = {
        "with_vaapi":   False,
        "with_ffmpeg":  True,
        "with_webrtc":  True,
        "with_mqtt":    True,
    }

    def requirements(self):
        if self.options.with_ffmpeg:
            self.requires("ffmpeg/6.1")
        if self.options.with_mqtt:
            self.requires("paho-mqtt-cpp/1.3.2")
        # Qt6 is expected from system installation; not via Conan in this project
        # self.requires("qt/6.6.1")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["ENABLE_FFMPEG"]  = self.options.with_ffmpeg
        tc.variables["ENABLE_VAAPI"]   = self.options.with_vaapi
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

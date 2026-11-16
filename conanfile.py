import os

from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout
from conan.tools.files import copy


class InMemDBConan(ConanFile):
    name = "inmemdb"
    version = "1.0.0"
    license = "MIT"
    url = "https://github.com/FuhMhanShu/InMemDB"
    description = (
        "In-Memory Document Store als eingebettete C++17-Bibliothek: schema-lose Dokumente, "
        "typsichere Queries + String-DSL, Indizes, Transaktionen, verschluesselte Snapshots - "
        "ohne externe Pflichtabhaengigkeiten."
    )
    topics = ("in-memory-database", "embedded", "document-store", "cpp17", "cmake")
    settings = "os", "compiler", "build_type", "arch"
    exports_sources = "CMakeLists.txt", "cmake/*", "include/*", "src/*", "LICENSE"

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()
        copy(self, "LICENSE", src=self.source_folder,
             dst=os.path.join(self.package_folder, "licenses"))

    def package_info(self):
        self.cpp_info.set_property("cmake_find_mode", "both")
        self.cpp_info.libs = ["memdb"]

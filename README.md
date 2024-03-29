![GitHub release (latest by date)](https://img.shields.io/github/v/release/ImageProcessing-ElectronicPublications/libiqa)
![GitHub Release Date](https://img.shields.io/github/release-date/ImageProcessing-ElectronicPublications/libiqa)
![GitHub repo size](https://img.shields.io/github/repo-size/ImageProcessing-ElectronicPublications/libiqa)
![GitHub all releases](https://img.shields.io/github/downloads/ImageProcessing-ElectronicPublications/libiqa/total)
![GitHub](https://img.shields.io/github/license/ImageProcessing-ElectronicPublications/libiqa)

# libiga

[Doxygen documentation](http://tdistler.com/iqa)

### BUILD:

All build artifacts end up in build/<configuration>, where <configuration> is 'debug' or 'release'.

* Windows:
    - Open iqa.sln, select 'Debug' or 'Release', and build. The output is a static library 'iqa.lib'.
    - To run the tests under the debugger, first right-click the 'test' project, select Properties -> Configuration Properties -> Debugging and set 'Working Directory' to '$(OutDir)'. Then start the application.

* Linux:
    - Change directories into the root of the IQA branch you want to build.
    - Type `make DEBUG=1` for a debug build, or `make` for a release build. The output is a static library 'libiqa.a'.
    - Type `make test` (or `make test DEBUG=1`) to build the unit tests.
    - Type `make clean` (or `make clean DEBUG=1`) to delete all build artifacts.
    - To run the tests, `cd` to the build/<configuration> directory and type `./test`.


### USE:

  - Include 'iqa.h' in your source file.
  - Call iqa_* methods.
  - Link against the IQA library.

### TEST IMAGES:

[Images for tests](test/resources/) are in [libiqa-testimages](https://github.com/ImageProcessing-ElectronicPublications/libiqa-testimages)

### HELP & SUPPORT:

[Further help](https://sourceforge.net/projects/iqa/support)

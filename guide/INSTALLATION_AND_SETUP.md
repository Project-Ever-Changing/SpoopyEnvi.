Installation and Setup
====

Spoopy Engine relies heavily on [Lime](https://lime.openfl.org) and would work best using Lime 8.0.0. The framework is made with [Haxe](http://www.haxe.org/download) as its primary language. Make sure to have the [Vulkan SDK](https://vulkan.lunarg.com/) downloaded for the backend side to work.

If you're on Mac OS or Linux, I recommend installing Haxe with [HomeBrew](https://brew.sh).

### <ins>**Lime Installation:**</ins>

If you have not setup Lime yet, start installing it by running this command `haxelib install lime 8.0.0`.

Afterwards, run `haxelib run lime setup`.


### <ins>**Spoopy Installation:**</ins>

If both Haxe and Lime are appropriately set up, install the repository using `haxelib git spoopy https://github.com/Project-Ever-Changing/spoopy`.

Similar to Lime, setup the repository with `haxelib run spoopy setup`.

If you are planning to compile natively to either C++ or Neko, make sure to build an NDLL using the following `spoopy build_ndll` or `haxelib run spoopy build_ndll`.

## Creating a Spoopy Project

You can create a template project using these series of commands:

    spoopy create <project_name>
    cd <project_path>
    spoopy test <platform>

When asked about `Project Path:`, insert the directory where you want your project to be stored, which `<project_path>` refers too. 

To compile your project through `debug mode` use `spoopy test <platform> -debug`

## Updating

When updating the framework, use `spoopy update -git`. If you get any errors while compiling a project that could involve C++, the best solution would be to update using `spoopy update -git -ndll`.

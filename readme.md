# A simple to use scenegraph system for raylib

The main motivation for this library is that it currently is not trivial to load 
and render 3D scene data in raylib without loosing several aspects that are 
important for working with 3D scenes:

* Named objects
* Hierarchical transformations
* Local transforms

All this information is lost when loading a 3D model using the current raylib 
API (as of 5.5).

This library aspires to provide a simple to use scenegraph system that can be
used to load and render 3D scenes using exchange formats like GLTF.

Additionally, the implementation should take care of handling culling and
provide tools to render scene content efficiently (e.g. by using instancing).

Furthermore, the system should be extendable to allow storing additional
metadata for objects. 

The API should be simple to use and should not require the user to deal with
pointers or memory management. Loading and unloading of scenes should be
as simple as it is with the current raylib API.

The discussion about the design of the API can be found in this 
github discussion: https://github.com/raysan5/raylib/discussions/4495

# Current state

The current state of the library is not much more than a draft of the API.
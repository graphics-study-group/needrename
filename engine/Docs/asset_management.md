# Asset Management

The asset management system is responsible for handling various types of game assets, such as textures, models, materials, and game object prefabs. It ensures that these assets are efficiently loaded, managed, and utilized during the game's runtime.

## How an External Resource Operates Within a Game

Taking an OBJ format model as an example:

Firstly, when the engine is running, it loads a project folder and reads the existing assets within the project.

When the project imports an external OBJ model resource, the *Asset Manager* reads the external OBJ resource, retrieves the associated triangular mesh model data, material data, and texture data, converts it into the game's internal asset format, and assigns a *Globally Unique Identifier* (GUID). It is stored in the ```assets``` directory of the project folder, with each resource becoming a ```name.type.asset``` file, which may include a ```name.type``` data file if necessary, where .asset is in ```JSON``` format. The ```.asset``` file stores the asset's reference relationships (represented by GUIDs) and some basic data. At this time, the Asset Manager will also import the asset into the system, which is a mapping from a GUID to a path.

When the engine loads a *level*, the level will call all internal *GameObject*s to load all their assets. At this point, all assets will query the path through the GUID to the Asset Manager, read the referenced resource files, and load them into memory or video memory.

After the loading is complete, when the world system calls *Tick*, these *GameObject*s can operate within the game.

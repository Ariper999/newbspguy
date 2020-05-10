#include "util.h"
#include "BspMerger.h"
#include <string>
#include <algorithm>
#include <iostream>
#include "CommandLine.h"

// super todo:
// game crashes randomly, usually a few minutes after not focused on the game (maybe from edit+restart?)

// minor todo:
// trigger_changesky for series maps with different skies
// warn about game_playerjoin and other special names
// fix spawners for things with custom keyvalues (apache, osprey, etc.)
// dump model info for the rest of the data types

// refactoring:
// save data structure pointers+sizes in Bsp class instead of copy-pasting them everywhere
// stop mixing printf+cout
// create progress-printing class instead of the methods used now


// Ideas for commands:
// optimize:
//		- merges redundant submodels (copy-pasting a picard coin all over the map)
//		- conditionally remove hull2 or func_illusionary clipnodes
// copymodel:
//		- copies a model from the source map into the target map (for adding new perfectly shaped brush ents)
// addbox:
//		- creates a new box-shaped brush model (faster than copymodel if you don't need anything fancy)
// info (default command if none set):
//		- check how close the map is to each BSP limit
// extract:
//		- extracts an isolated room from the BSP
// decompile:
//      - to RMF. Try creating brushes from convex face connections?
// export:
//      - export BSP models to MDL models.
// clip:
//		- replace the clipnodes of a model with a simple bounding box.

const char* version_string = "bspguy v1 (May 2020)";

int test() {
	/*
	Bsp test("echoes14.bsp");
	//test.strip_clipping_hull(2);
	test.move(vec3(64, 64, 64));
	test.write("yabma_move.bsp");
	test.write("D:/Steam/steamapps/common/Sven Co-op/svencoop_addon/maps/yabma_move.bsp");
	return 0;
	*/

	vector<Bsp*> maps;
	/*
	for (int i = 1; i < 22; i++) {
		Bsp* map = new Bsp("saving_the_2nd_amendment" + (i > 1 ? to_string(i) : "") + ".bsp");
		map->strip_clipping_hull(2);
		maps.push_back(map);
	}
	*/

	//maps.push_back(new Bsp("echoes01.bsp"));
	//maps.push_back(new Bsp("echoes01a.bsp"));
	//maps.push_back(new Bsp("echoes02.bsp"));

	//maps.push_back(new Bsp("echoes03.bsp"));
	//maps.push_back(new Bsp("echoes04.bsp"));
	//maps.push_back(new Bsp("echoes05.bsp"));

	//maps.push_back(new Bsp("echoes06.bsp"));
	//maps.push_back(new Bsp("echoes07.bsp"));

	//maps.push_back(new Bsp("echoes09.bsp"));
	//maps.push_back(new Bsp("echoes09a.bsp"));

	//maps.push_back(new Bsp("echoes09b.bsp"));
	//maps.push_back(new Bsp("echoes10.bsp"));

	//maps.push_back(new Bsp("echoes12.bsp"));
	//maps.push_back(new Bsp("echoes13.bsp"));

	maps.push_back(new Bsp("echoes01.bsp"));
	maps.push_back(new Bsp("echoes02.bsp"));

	for (int i = 0; i < maps.size(); i++) {
		maps[i]->strip_clipping_hull(2);
	}

	BspMerger merger;
	Bsp* result = merger.merge(maps, vec3(0, 0, 0), false);
	printf("\n");
	if (result != NULL) {
		result->write("yabma_move.bsp");
		result->write("D:/Steam/steamapps/common/Sven Co-op/svencoop_addon/maps/yabma_move.bsp");
		result->print_info(false, 0, SORT_CLIPNODES);
		result->print_info(true, 10, SORT_CLIPNODES);
	}
	return 0;
}

int merge_maps(CommandLine& cli) {
	
	vector<string> input_maps = cli.getOptionList("-maps");

	if (input_maps.size() < 2) {
		cout << "ERROR: at least 2 input maps are required\n";
		return 1;
	}

	vector<Bsp*> maps;

	for (int i = 0; i < input_maps.size(); i++) {
		Bsp* map = new Bsp(input_maps[i]);
		if (!map->valid)
			return 1;
		maps.push_back(map);
	}

	if (cli.hasOption("-nohull2")) {
		printf("Stripping hull 2 from each input map...\n");
		int removed = 0;
		for (int i = 0; i < maps.size(); i++) {
			removed += maps[i]->strip_clipping_hull(2);
		}
		printf("Deleted %d clipnodes\n\n", removed);
	}	
	
	vec3 gap = cli.hasOption("-gap") ? cli.getOptionVector("-gap") : vec3(0,0,0);

	BspMerger merger;
	Bsp* result = merger.merge(maps, gap, cli.hasOption("-noripent"));

	printf("\n");
	result->write(cli.hasOption("-o") ? cli.getOption("-o") : cli.bspfile);
	printf("\n");
	result->print_info(false, 0, 0);

	for (int i = 0; i < maps.size(); i++) {
		delete maps[i];
	}
}

int print_info(CommandLine& cli) {
	Bsp* map = new Bsp(cli.bspfile);
	if (!map->valid)
		return 1;

	bool limitMode = false;
	int listLength = 10;
	int sortMode = SORT_CLIPNODES;

	if (cli.hasOption("-limit")) {
		string limitName = cli.getOption("-limit");
			
		limitMode = true;
		if (limitName == "clipnodes") {
			sortMode = SORT_CLIPNODES;
		}
		else if (limitName == "nodes") {
			sortMode = SORT_NODES;
		}
		else if (limitName == "faces") {
			sortMode = SORT_FACES;
		}
		else if (limitName == "vertexes") {
			sortMode = SORT_VERTS;
		}
		else {
			cout << "ERROR: invalid limit name: " << limitName << endl;
			return 0;
		}
	}
	if (cli.hasOption("-all")) {
		listLength = 32768; // should be more than enough
	}

	map->print_info(limitMode, listLength, sortMode);

	delete map;

	return 0;
}

int noclip(CommandLine& cli) {
	Bsp* map = new Bsp(cli.bspfile);
	if (!map->valid)
		return 1;

	int model = -1;
	int hull = -1;

	if (cli.hasOption("-hull")) {
		hull = cli.getOptionInt("-hull");

		if (hull < 0 || hull >= MAX_MAP_HULLS) {
			cout << "ERROR: hull number must be 1-3\n";
			return 1;
		}
	}

	int numDeleted = 0;
	if (cli.hasOption("-model")) {
		model = cli.getOptionInt("-model");

		int modelCount = map->header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL);

		if (model < 0) {
			cout << "ERROR: model number must be 0 or greater\n";
			return 1;
		}
		if (model >= modelCount) {
			printf("ERROR: there are only %d models in this map\n", modelCount);
			return 1;
		}

		numDeleted = map->strip_clipping_hull(hull, model, false);
	}
	else {
		if (hull != -1) {
			numDeleted = map->strip_clipping_hull(hull);
		}
		else {
			for (int i = 1; i < MAX_MAP_HULLS; i++) {
				numDeleted += map->strip_clipping_hull(i);
			}
		}
	}

	printf("Deleted %d clipnodes\n", numDeleted);
	map->write(cli.hasOption("-o") ? cli.getOption("-o") : map->path);
	printf("\n");

	map->print_info(false, 0, 0);

	delete map;
}

int transform(CommandLine& cli) {
	Bsp* map = new Bsp(cli.bspfile);
	if (!map->valid)
		return 1;

	vec3 move;

	if (cli.hasOptionVector("-move")) {
		move = cli.getOptionVector("-move");

		printf("Applying offset (%.2f, %.2f, %.2f)\n",
			move.x, move.y, move.z);

		map->move(move);
	}
	else {
		printf("ERROR: at least one transformation option is required\n");
		return 1;
	}
	
	map->write(cli.hasOption("-o") ? cli.getOption("-o") : map->path);
	printf("\n");

	map->print_info(false, 0, 0);

	delete map;
}

void print_help(string command) {
	if (command == "merge") {
		cout <<
			"merge - Merges two or more maps together\n\n"

			"Usage:   bspguy merge <mapname> -maps \"map1, map2, ... mapN\" [options]\n"
			"Example: bspguy merge merged.bsp -maps \"svencoop1, svencoop2\"\n"

			"\n[Options]\n"
			"  -nohull2     : Strip collision hull 2 from each map before merging.\n"
			"  -noripent    : By default, the input maps are assumed to be part of a series.\n"
			"                 Level changes and other things are updated so that the merged\n"
			"                 maps can be played one after another. This flag prevents any\n"
			"                 entity edits from being made (except for origins).\n"
			"  -gap \"X,Y,Z\" : Amount of extra space to add between each map\n"
			;
	}
	else if (command == "info") {
		cout <<
			"info - Show BSP data summary\n\n"

			"Usage:   bspguy info <mapname> [options]\n"
			"Example: bspguy info svencoop1.bsp -limit clipnodes -all\n"

			"\n[Options]\n"
			"  -limit <name> : List the models contributing most to the named limit.\n"
			"                  <name> can be one of: [clipnodes, nodes, faces, vertexes]\n"
			"  -all          : Show the full list of models when using -limit.\n"
			;
	}
	else if (command == "noclip") {
		cout <<
			"noclip - Delete some clipnodes from the BSP\n\n"

			"Usage:   bspguy noclip <mapname> [options]\n"
			"Example: bspguy noclip svencoop1.bsp -hull 2\n"

			"\n[Options]\n"
			"  -model #  : Model to strip collision from. By default, all models are stripped.\n"
			"  -hull #   : Collision hull to strip (1-3). By default, all hulls are stripped.\n"
			"              1 = Human-sized monsters and standing players\n"
			"              2 = Large monsters and func_pushable\n"
			"              3 = Small monsters, crouching players, and melee attacks\n"
			"  -o <file> : Output file. By default, <mapname> is overwritten.\n"
			;
	}
	else if (command == "transform") {
		cout <<
			"transform - Apply 3D transformations\n\n"

			"Usage:   bspguy transform <mapname> [options]\n"
			"Example: bspguy transform svencoop1.bsp -move \"0,0,1024\"\n"

			"\n[Options]\n"
			"  -move \"X,Y,Z\" : Units to move the map on each axis.\n"
			"  -o <file>     : Output file. By default, <mapname> is overwritten.\n"
			;
	}
	else {
		cout << version_string << endl << endl <<
			"This tool modifies Sven Co-op BSPs without having to decompile them.\n\n"
			"Usage: bspguy <command> <mapname> [options]\n"

			"\n<Commands>\n"
			"  info      : Show BSP data summary\n"
			"  merge     : Merges two or more maps together\n"
			"  noclip    : Delete some clipnodes from the BSP\n"
			"  transform : Apply 3D transformations to the BSP\n"

			"\nRun 'bspguy <command> help' to read about a specific command.\n"
			;
	}
}

int main(int argc, char* argv[])
{
	//test();

	CommandLine cli(argc, argv);

	if (cli.askingForHelp) {
		print_help(cli.command);
		return 0;
	}

	if (cli.command == "version" || cli.command == "--version" || cli.command == "-version" || cli.command == "-v") {
		printf(version_string);
		return 0;
	}

	if (cli.bspfile.empty()) {
		cout << "ERROR: no map specified\n"; return 1;
	}

	if (cli.command == "info") {
		return print_info(cli);
	}
	else if (cli.command == "noclip") {
		return noclip(cli);
	}
	else if (cli.command == "transform") {
		return transform(cli);
	}
	else if (cli.command == "merge") {
		return merge_maps(cli);
	}
	else {
		cout << "unrecognized command: " << cli.command << endl;
	}

	return 0;
}


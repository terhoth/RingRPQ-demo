/*
 * File: find-paths.cpp
 * Author: Terho Thorel
 * Description: finds paths of given length from a given node
 *
 */

#include <iostream>
#include <fstream>
#include <sdsl/construct.hpp>
#include "ring_rpq_attr.hpp"

using namespace std;

using namespace std::chrono;
using timer = std::chrono::high_resolution_clock;


int main(int argc, char **argv) 
{
	cout << "  Usage: " << argv[0] 
	     << " <ring-index-file> <start-node-name> <path-length> " << endl;
	cout << "  (Give a negative length to search backward)" << endl;
	if (argc < 4) return EXIT_FAILURE;
	string start_name = string(argv[2]);
	int path_length_arg = stoul(argv[3]);
	unsigned int path_length = abs(path_length_arg);
	bool backward = (path_length_arg < 0);

	// Open input files
	string filename = argv[1];
	ifstream ifs(filename.c_str());
	if (!ifs.is_open()) throw invalid_argument("Could not open" + filename);
	ifstream ifs_SO(filename + ".SO", ifstream::in);
	if (!ifs_SO.is_open()) throw invalid_argument("Could not open" + filename + ".SO");
	ifstream ifs_P(filename + ".P", ifstream::in);
	if (!ifs_P.is_open()) throw invalid_argument("Could not open" + filename + ".P");
	cout << "Opened file " << filename << " successfully" << endl;

	// Load the index
	auto load_start = timer::now();
	ring_rpq_attr_bfs graph;
	graph.load(filename);
	auto load_end = timer::now();
	int load_time = duration_cast<milliseconds>(load_end - load_start).count();
	cout << "  Index loaded " 
	     << (float) graph.size() / (graph.n_triples() / 2) << " bytes per triple " 
	     << load_time << " ms" << endl;

	// find the paths
	vector<vector<string>> paths;
	graph.find_paths(start_name, path_length, paths, !backward, backward);
	
	// (lambda to remove angle brackets from node/predicate names)
	auto trim = [](string str) 
	            { return str.length() >= 2 ? str.substr(1, str.length()-2) : ""; };

	// print the results
	cout << "Found " << paths.size() << " paths:" << endl;
	for (auto path : paths)
	{
		string end_name = path.back();
		path.pop_back();

		cout << trim(start_name);
		if (backward) cout << "  <--"; else cout << "  --";
		for (string p : path)
		{
			cout << trim(p) << "-";
		}
		if (backward) cout << "-  "; else cout << "->  ";
		cout << trim(end_name) << endl;
	}
		

	return EXIT_SUCCESS;
}


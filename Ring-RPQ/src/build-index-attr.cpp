/*
 * File: build-index-attr.cpp
 * Author: modified by Terho Thorel, from build-index-basic.cpp
 * Description: When executed from command line 
 *    with the path to the data directory's .dat file as argument,
 *    constructs the ring_rpq_attr index and saves it to the same directory.
 *
 */


#include <iostream>
#include <fstream>
#include <sdsl/construct.hpp>
#include "ring_rpq_attr.hpp"
#include "parse_query_attr.cpp"
#include "load_index_util.cpp"
#include "attr_info.hpp"

using namespace std;

using namespace std::chrono;
using timer = std::chrono::high_resolution_clock;


int main(int argc, char **argv)
{
	// Open the data file
	if (argc < 2)
	{
		cout << "Usage: " << argv[0] << " <path-to-dat> " << endl;
		return EXIT_FAILURE;
	}
	string filename = argv[1];

	ring_rpq_attr_bfs graph;

	timer::time_point start;
	timer::time_point stop;
	unsigned int time;

	// construct the index
	start = timer::now();
	graph.construct(filename, true);
	stop = timer::now();
	time = duration_cast<seconds>(stop-start).count();
	cout << "Index built in " << time << " seconds." << endl;

	// save the index
	start = timer::now();
	graph.save(filename);
	stop = timer::now();
	time = duration_cast<seconds>(stop-start).count();
	cout << "Index saved in " << time << " seconds." << endl;

	return EXIT_SUCCESS;
}


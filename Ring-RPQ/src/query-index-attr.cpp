/*
 * File: query-index-attr.cpp
 * Author: Terho Thorel
 * Description: 
 * 	 To be executed from command line with paths to
 *     1. the .dat file in the data directory
 *     2. the file containing queries to run
 *   Evaluates the queries one by one using a ring index,
 *   which is assumed to already have been built using build-index-attr.
 *   Prints the number of results and evaluation time for each query.
 *   If there are only few results, prints them too.
 *
 */

#include <iostream>
#include <fstream>
#include <sdsl/construct.hpp>
#include <regex>
#include "ring_rpq_attr.hpp"
#include "parse_query_attr.cpp"
#include "load_index_util.cpp"

using namespace std;

using namespace std::chrono;
using timer = std::chrono::high_resolution_clock;


int main(int argc, char **argv) 
{
	if (argc < 3)
	{
		cout << "  Usage: " << argv[0] << " <ring-index-file> <queries-file> " << endl;
		cout << "     Optional 3rd argument: max results to print (default 0)" << endl;
		cout << "     Optional 4th argument: max results to search (default: inf)" << endl;
		return EXIT_FAILURE;
	}
	
	std::ifstream ifs_q(argv[2]);
	if (!ifs_q.is_open()) throw invalid_argument("Could not open query file");

	// optional arguments
	unsigned int max_results_to_print = 0;
	if (argc >= 4)
	{
		max_results_to_print = stoi(argv[3]);
	}
	uint64_t bound = std::numeric_limits<uint64_t>::max();
	if (argc >= 5)
	{
		bound = stoul(argv[4]);
	}

	// Load the index
	auto load_start = timer::now();
	ring_rpq_attr_bfs graph;
	graph.load(argv[1]);
	auto load_end = timer::now();
	int load_time = duration_cast<milliseconds>(load_end - load_start).count();
	cout << "  Index loaded " 
	     << (float) graph.size() / (graph.n_triples() / 2) << " bytes per triple " 
	     << load_time << " ms" << endl;

	// Initialize query variables
	vector<pair<uint64_t, uint64_t>> query_output;
	string line;
	high_resolution_clock::time_point start, stop;
	duration<double> time_span;

	// Now the actual queries!
	cout << "  Now querying... " << endl;
	unsigned int q = 0;
	while (getline(ifs_q, line))
	{
		// skip comment lines
		if (line.length() == 0 or line.at(0) == '#') continue;

		q++;
		cout << "Evaluating RPQ: " <<  line << endl;

		// reset query-specific variables
		start = high_resolution_clock::now();
		query_output.clear();

		ParseError error = graph.evaluate(line, query_output, bound, TIME_OUT);
		if (error != ParseError::None) cout << q << ";" << " Parse Error" << endl;

		// Print the results
		
		stop = high_resolution_clock::now();
		time_span = duration_cast<microseconds>(stop - start);

		cout << q << ";" << query_output.size() 
		          << ";" << (uint64_t) (time_span.count() * 1000000000ULL) << endl;

		if (query_output.size() > 0 and max_results_to_print > 0)
		{
			cout << "Results:" << endl;
			unsigned int i = 0;
			for (auto match : query_output)
			{
				cout << "  " << match.first << " --> " << match.second << endl;
				i++;
				if (i == max_results_to_print) break;
			}
			if (query_output.size() > max_results_to_print)
			{
				cout << "  ..." << endl;
			}
		}
	}

	return EXIT_SUCCESS;
}


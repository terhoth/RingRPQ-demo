/*
 * File: test-correctness.cpp
 * Author: Terho Thorel
 * Description: 
 * 	 To be executed from command line with paths to
 *     1. the .dat file in the data directory
 *     2. the file containing queries to run
 *     3. the file containing expected answers to the queries
 *   These can be omitted to use default tests.
 *   Alternatively a single command line parameter can be given
 *   as shortcut to narrower test sets: basic, attr, node_attr, branching.
 *
 *   Evaluates the queries one by one using a ring index,
 *   which is assumed to already have been built using build-index-attr.
 *   Reports if a query's output does not match the expected answers.
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

const string TEST_DIRECTORY = "../data/test_correctness/";
const string INDEX_FILE = TEST_DIRECTORY + "test.dat";
const string QUERY_FILE = TEST_DIRECTORY + "paths_full.tsv";
const string ANSWER_FILE = TEST_DIRECTORY + "answers_full.tsv";

int main(int argc, char **argv) 
{
	cout << "  Usage: " << argv[0] << " <ring-index-file> <queries-file> <expected-answers-file>" << endl;
	cout << "  Default: " << INDEX_FILE << " " << QUERY_FILE << " " << ANSWER_FILE << endl;
	cout << "  Shortcuts: " << "full, basic, attr, node_attr, branching" << endl;

	string index_file, query_file, answer_file;
	if (argc == 4) // non-default files provided by user
	{
		index_file = argv[1];
		query_file = argv[2];
		answer_file = argv[3];
	}
	else if (argc == 2) // shortcut
	{
		index_file = INDEX_FILE;
		query_file = TEST_DIRECTORY + "paths_" + string(argv[1]) + ".tsv";
		answer_file = TEST_DIRECTORY + "answers_" + string(argv[1]) + ".tsv";
	}
	else // default
	{
		index_file = INDEX_FILE;
		query_file = QUERY_FILE;
		answer_file = ANSWER_FILE;
	}

	// open input files
	ifstream ifs_q(query_file);
	if (!ifs_q.is_open())   throw std::invalid_argument("Could not open" + query_file);
	ifstream ifs_ans(answer_file);
	if (!ifs_ans.is_open()) throw std::invalid_argument("Could not open" + answer_file);

	// load the index
	auto load_start = timer::now();
	ring_rpq_attr_bfs graph;
	graph.load(index_file);
	auto load_end = timer::now();
	int load_time = duration_cast<milliseconds>(load_end - load_start).count();
	cout << "  Index loaded " 
	     << (float) graph.size() / (graph.n_triples() / 2) << " bytes per triple " 
	     << load_time << " ms" << endl;

	// initialize query variables
	vector<pair<uint64_t, uint64_t>> query_output;
	string line;

	// Parse and run the queries
	cout << "Starting tests" << endl;
	int queries_passed = 0;
	int queries_failed = 0;
	unsigned int q = 0;
	while (getline(ifs_q, line))
	{
		if (line.length() == 0 or line.at(0) == '#') continue;

		q++;
		query_output.clear();

		ParseError error = graph.evaluate(line, query_output);
		if (error != ParseError::None)
		{
			cout << endl << "Parse error! " << line << endl; 	
			++queries_failed;
			continue;
		}

		// Compare output to expected answers:

		while (ifs_ans.peek() == '#') // skip comment lines
		{ 
			ifs_ans.ignore(numeric_limits<streamsize>::max(), '\n');
		}

		// convert output to set
		unordered_set<pair<uint64_t, uint64_t>> output_set;
		for (auto pair : query_output)
		{
			output_set.insert(pair);
		}

		// convert answers to set
		unordered_set<pair<uint64_t, uint64_t>> answer_set;
		int expected_n_of_results;
		uint64_t s, o;
		ifs_ans >> expected_n_of_results;
		for (int i = 0; i < expected_n_of_results; i++)
		{
			ifs_ans >> s >> o;
			answer_set.emplace(s, o);
		}

		// check that the line ends where it should
		if (ifs_ans.get() != '\n')
		{
			cout << endl << "Error: something off at query " << line 
			     << " in file " << answer_file << endl;
			return EXIT_FAILURE;
		}
		
		// compare and report
		if (output_set.size() != query_output.size())
		{
			++queries_failed;
			cout << endl << "Fail! " << line << endl;
			cout << "Found " << output_set.size() << " results, " 
			     << "expected " << answer_set.size() << endl;
			cout << "Duplicate results!" << endl;
			cout << endl;
		}
		else if (output_set != answer_set)
		{
			++queries_failed;

			cout << endl << "Fail! " << line << endl;

			cout << "Found " << output_set.size() << " results, " 
			     << "expected " << answer_set.size() << endl;

			cout << "Missing: ";
			for (auto answer_pair : answer_set)
			{
				if (output_set.find(answer_pair) == output_set.end())
				{
					cout << answer_pair.first << "-->" << answer_pair.second << " ";
				}
			}
			cout << endl;

			cout << "Superfluous: ";
			for (auto output_pair : output_set)
			{
				if (answer_set.find(output_pair) == answer_set.end())
				{
					cout << output_pair.first << "-->" << output_pair.second << " ";
				}
			}
			cout << endl << endl;
		}
		else // output matches answers, yay!
		{
			++queries_passed;
			cout << ".";
		}
	} 

	cout << endl << "Passed " << queries_passed 
	             << " out of " << queries_passed + queries_failed << "." << endl;

	return EXIT_SUCCESS;
}


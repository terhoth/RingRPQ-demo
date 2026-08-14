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

	// load the index
	auto load_start = timer::now();
	ring_rpq_attr_bfs graph;
	graph.load(index_file);
	auto load_end = timer::now();
	int load_time = duration_cast<milliseconds>(load_end - load_start).count();
	cout << "  Index loaded " 
	     << (float) graph.size() / (graph.n_triples() / 2) << " bytes per triple " 
	     << load_time << " ms" << endl;

// Temporarily does not work
/*
	// initialize query variables
	vector<pair<uint64_t, uint64_t>> query_output;
	unordered_map<string, uint64_t> pred_map;
	unordered_map<string, AttrInfo> attr_map;
	string line, rpq, query, query_type;
	vector<uint64_t> pred_vec;
	uint64_t s_id = 0;
	uint64_t o_id = 0;
	vector<string> subqueries;

	// Parse and run the queries
	cout << "Starting tests" << endl;
	int queries_passed = 0;
	int queries_failed = 0;
	unsigned int q = 0;
	while (getline(ifs_q, line))
	{
		if (line.length() == 0 or line.at(0) == '#') continue;

		q++;

		// reset query-specific variables
		pred_map.clear();
		pred_map[NODE_PREDICATE] = map_P.at(NODE_PREDICATE);
		pred_map[WILDCARD_PREDICATE] = map_P.at(WILDCARD_PREDICATE);
		attr_map.clear();
		rpq.clear();
		query.clear();
		query_type.clear();
		pred_vec.clear();
		subqueries.clear();
		query_output.clear();

		// separate subject, rpq and object from the query line
		tie(s_id, rpq, o_id) = parse_subject_and_object(line, map_SO);
		if (rpq == "") 
		{ 
			cout << endl << "Fail! " << line << endl;
			cout << "Parse error: could not find subject and/or object" << endl << endl; 
			queries_failed++;
			continue; 
		}

		// extract possible branching subqueries into separate queries
		rpq = extract_nested_subqueries(rpq, subqueries);
		if (rpq == "") 
		{ 
			cout << endl << "Fail! " << line << endl;
			cout << "Parse error: mismatched or misplaced curly brackets" << endl << endl; 
			queries_failed++;
			continue; 
		}
		
		// evaluate the subqueries (if any)
		graph.prepare_nested_query(attr_map, subqueries.size());
		for (unsigned int sub_q = 0; sub_q < subqueries.size(); sub_q++)
		{	
			// parse
			ParseError error = parse_query(subqueries.at(sub_q), query, 
			                               query_type, pred_vec, 
			                               map_P, pred_map, map_A, attr_map);
			if (error != ParseError::None)
			{
				cout << endl << "Fail! " << line << " (subquery " << sub_q << ")" << endl;
				cout << "Parse error: " << int(error) << endl << endl;
				queries_failed++;
				continue;
			}

			// evaluate
			bool is_trivial = graph.run_subquery(sub_q, query, pred_map, attr_map);
			if (is_trivial)
			{		
				// remove requirements related to this subquery, as they are trivially true
				auto replace_regex = regex("\\[_" + to_string(sub_q) + "_ = 1\\]"); 
				for (unsigned int _q = sub_q + 1; _q < subqueries.size(); _q++)
				{
					subqueries.at(_q) = regex_replace(subqueries.at(_q), replace_regex, "");
				}
				rpq = regex_replace(rpq, replace_regex, "");
			}
		}

		// evaluate the main query
		ParseError error = parse_query(rpq, query, query_type, pred_vec, 
		                               map_P, pred_map, map_A, attr_map);
		if (error != ParseError::None)
		{
			cout << endl << "Fail! " << line << endl;
			cout << "Parse error: " << int(error) << endl << endl;
			queries_failed++;
			continue;
		}
		graph.run_query(query, query_type, pred_vec, s_id, o_id, 
						pred_map, attr_map, query_output);
		graph.cleanup_nested_query(subqueries.size());



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
		if (output_set != answer_set)
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
*/

	return EXIT_SUCCESS;
}


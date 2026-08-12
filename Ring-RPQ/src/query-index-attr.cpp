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

	// Open input files
	string filename = argv[1];
	ifstream ifs(filename.c_str());
	if (!ifs.is_open()) throw invalid_argument("Could not open" + filename);
	ifstream ifs_SO(filename + ".SO", ifstream::in);
	if (!ifs_SO.is_open()) throw invalid_argument("Could not open" + filename + ".SO");
	ifstream ifs_P(filename + ".P", ifstream::in);
	if (!ifs_P.is_open()) throw invalid_argument("Could not open" + filename + ".P");
	ifstream ifs_q(argv[2], ifstream::in);
	if (!ifs_q.is_open()) throw invalid_argument("Could not open" + string(argv[2]));
	cout << "Opened file " << filename << " successfully" << endl;

	// Get attribute info (id, Edge/Node, Str/Int/Flt)
	unordered_map<string, AttrInfo> map_A;
	load_attribute_info(filename, map_A);

	// Load the index
	auto load_start = timer::now();
	ring_rpq_attr_bfs graph;
	graph.load(filename);
	auto load_end = timer::now();
	int load_time = duration_cast<milliseconds>(load_end - load_start).count();
	cout << "  Index loaded " 
	     << (float) graph.size() / (graph.n_triples() / 2) << " bytes per triple " 
	     << load_time << " ms" << endl;

	// Map names to ids
	unordered_map<string, uint64_t> map_SO;
	unordered_map<uint64_t, string> map_SO_to_name;
	unordered_map<string, uint64_t> map_P;
	uint64_t id;
	string name;
	while (ifs_SO >> id >> name)
	{
		map_SO[name] = id;
		map_SO_to_name[id] = name;
		ifs_SO.ignore(numeric_limits<streamsize>::max(), '\n');
	}
	uint64_t max_p = 0;
	while (ifs_P >> id >> name)
	{
		map_P[name] = id;
		if (id > max_p) max_p = id;
		ifs_P.ignore(numeric_limits<streamsize>::max(), '\n');
	}
	// give the node symbol an unused id
	map_P[NODE_PREDICATE] = max_p + 1;
    // give the wildcard edge an unused id
    map_P[WILDCARD_PREDICATE] = 0;


	
	// Initialize query variables
	vector<pair<uint64_t, uint64_t>> query_output;
	unordered_map<string, uint64_t> pred_map;
	unordered_map<string, AttrInfo> attr_map;
	string line, rpq, query, query_type;
	vector<uint64_t> pred_vec;
	uint64_t s_id = 0;
	uint64_t o_id = 0;
	vector<string> subqueries;
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
			cout << q << "; Parse error: could not find subject and/or object" << endl; 
			continue; 
		}

		// extract possible branching subqueries into separate queries
		rpq = extract_nested_subqueries(rpq, subqueries);
		if (rpq == "") 
		{ 
			cout << q << "; Parse error: mismatched or misplaced curly brackets" << endl; 
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
				cout << "   Parse error in subquery" << endl;
				continue;
			}

			// evaluate (results are stored internally)
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
			cout << q << "; Parse error: ";
			if      (error == ParseError::UnknownPredicate) cout << "unknown predicate";
			else if (error == ParseError::UnknownAttribute) cout << "unknown attribute";
			else cout << "invalid syntax";
			cout << endl;
			continue;
		}
		graph.run_query(query, query_type, pred_vec, s_id, o_id, 
		                pred_map, attr_map, query_output, bound);
		graph.cleanup_nested_query(subqueries.size());

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
				cout << "  " << map_SO_to_name.at(match.first) << " --> "
				            << map_SO_to_name.at(match.second) << endl;
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


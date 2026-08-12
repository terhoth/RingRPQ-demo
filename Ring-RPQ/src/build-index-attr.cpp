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
	ifstream ifs(filename.c_str());
	if (!ifs.is_open()) throw std::invalid_argument("Could not open " + filename);
	cout << "Opened " << filename << " successfully" << endl;

	// Read attribute names and types, 
	// and count how many attributes there are
	cout << "preprocessing attributes..." << endl;
	unordered_map<string, AttrInfo> attr_map;
	vector<AttrInfo> edge_attr_infos;
	vector<AttrInfo> node_attr_infos;
	auto attr_counts = load_attribute_info(filename, attr_map, &edge_attr_infos, &node_attr_infos);
	int n_of_str_attrs = get<0>(attr_counts); 
	int n_of_int_attrs = get<1>(attr_counts); 
	int n_of_flt_attrs = get<2>(attr_counts); 

	// initialize maps for storing attributes values
	vector<unordered_map<uint64_t, string>> str_attrs(n_of_str_attrs);
	vector<unordered_map<uint64_t, int>> int_attrs(n_of_int_attrs);
	vector<unordered_map<uint64_t, float>> flt_attrs(n_of_flt_attrs);


	// Gather the triples into a vector Data, and read edge attributes

	// (the fourth uint64_t below is the triple's index, comparable to a edge id)
	using spo_triple_with_id = tuple<uint64_t, uint64_t, uint64_t, uint64_t>;
	vector<spo_triple_with_id> Data;

	cout << "reading triples..." << endl;
	uint64_t s, p, o;
	uint64_t triple_id = 0;
	string str_value;
	while (ifs >> s >> p >> o) // read triple
	{
		// read edge attribute values
		for (AttrInfo attr : edge_attr_infos)
		{
			ifs >> str_value;
			if (str_value != NULL_VALUE)
			{
				if (attr.type == AttrInfo::Str)
				{
					str_attrs.at(attr.id)[triple_id] = str_value;
				}
				else if (attr.type == AttrInfo::Int)
				{
					int_attrs.at(attr.id)[triple_id] = stoi(str_value);
				}
				else if (attr.type == AttrInfo::Flt)
				{
					flt_attrs.at(attr.id)[triple_id] = stof(str_value);
				}
			} 
		}
		// add the triple to Data
		Data.push_back(spo_triple_with_id(s, p, o, triple_id));
		triple_id++;
	}


	// find largest predicate
	ifstream P_ifs((filename + ".P").c_str());
	uint64_t max_p = 0;
	while (P_ifs >> p)
	{
		if (p > max_p) max_p = p;
		P_ifs.ignore(numeric_limits<streamsize>::max(), '\n');
	}

	// Read node attributes:
	cout << "reading nodes..." << endl;
	ifstream SO_ifs((filename + ".SO").c_str());
	uint64_t node_id;
	while (SO_ifs >> node_id)
	{
		for (AttrInfo attr : node_attr_infos)
		{
			SO_ifs >> str_value;
			if (str_value != NULL_VALUE)
			{
				if (attr.type == AttrInfo::Str)
				{
					str_attrs.at(attr.id)[triple_id] = str_value;
				}
				else if (attr.type == AttrInfo::Int)
				{
					int_attrs.at(attr.id)[triple_id] = stoi(str_value);
				}
				else if (attr.type == AttrInfo::Flt)
				{
					flt_attrs.at(attr.id)[triple_id] = stof(str_value);
				}
			}
		}
		// add a reflexive edge to store node attributes, with an unused predicate id
		Data.push_back(spo_triple_with_id(node_id, max_p + 1, node_id, triple_id));
		triple_id++;
	}

	Data.shrink_to_fit();
	uint64_t input_size = Data.size();
	
	cout << "--Indexing " << Data.size() << " triples " 
	     << "with " << attr_map.size() << " attributes "
	     << "(str: "<< n_of_str_attrs 
	     << ", int: " << n_of_int_attrs 
	     << ", flt: " << n_of_flt_attrs << ")" << endl;

	// construct the index
	auto start = timer::now();
	ring_rpq_attr_bfs A(Data, str_attrs, int_attrs, flt_attrs);
	auto stop = timer::now();

	cout << "  Index built " << (float)A.size()/input_size << " bytes per triple" << endl;

	A.save(filename);
	cout << "Index saved" << endl;
	cout << duration_cast<seconds>(stop-start).count() << " seconds." << endl;
	return 0;
}


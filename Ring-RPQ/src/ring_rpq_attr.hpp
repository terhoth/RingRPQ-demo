/*
 * File: ring_rpq_attr.hpp
 * Author: Modified by Terho Thorel, from ring_rpq_basic.hpp by Diego Arroyuelo
 * Description: A class representing a graph using the ring index structure.
 * 	Includes evaluation algorithms for regular path queries with attributes.
 */

/*
 * triple_bwt_rpq.hpp
 * Copyright (C) 2021 Diego Arroyuelo
 * 
 * This is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This software is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef RING_RPQ_ATTR
#define RING_RPQ_ATTR

#include <iostream>
#include <cstdint>
#include <chrono>
#include <set>
#include <unordered_set>
#include <cstdio>
#include <cstdlib>
#include <sdsl/init_array.hpp>

#include "bwt.hpp"
#include "bwt-C-nose.hpp"
#include "bwt_interval.hpp"
#include "utils.hpp"
#include "RpqAutomata_attr.hpp"
#include "RpqTree.hpp"
#include "query_config.hpp"
#include "parse_query_attr.cpp"
#include "wt_helper.hpp"
#include "attr_info.hpp"
#include "load_index_util.cpp"

#define ELEMENTS 0
#define RUN_QUERY 0

using namespace std::chrono;
using timer = std::chrono::high_resolution_clock;


typedef struct {
	bwt_interval interval;
	word_t current_D;
} interval_state_type;

typedef struct {
	uint64_t element;
	std::vector<std::pair<uint64_t, uint64_t>> solutions;
} element_solution_type;

// a triple's subject, predicate, object, and id
typedef std::tuple<uint64_t, uint64_t, uint64_t, uint64_t> spo_triple_with_id;




template<class Container = std::stack<interval_state_type>>
class ring_rpq_attr 
{

	// The wavelet tree L_S has been replaced with a int_vector,
	// to make the iteration of all triples in an interval possible.
	int_vector<> L_S;
	vector<uint64_t> C_S;
	bwt L_P;

	// Containers for attributes.
	// The outmost vector is indexed with attribute id's.
	// For each attribute, there is a mapping
	// from indices of L_S (which represent triples) to values.
	vector<unordered_map<uint64_t, string>> str_attributes;
	vector<unordered_map<uint64_t, int>> int_attributes;
	vector<unordered_map<uint64_t, float>> flt_attributes;

	uint64_t real_max_P;

	uint64_t max_S;
	uint64_t max_P;
	uint64_t max_O;
	uint64_t nTriples;  // number of triples
 
	std::unordered_map<uint64_t, std::string> P_id_to_name;
	std::unordered_map<std::string, uint64_t> P_name_to_id;
	std::unordered_map<uint64_t, std::string> SO_id_to_name;
	std::unordered_map<std::string, uint64_t> SO_name_to_id;
	std::unordered_map<std::string, AttrInfo> A_name_to_info;

	// query-specific vector containing the useful automata state sets for each bwt node
	std::vector<word_t> B_array;

public:

	ring_rpq_attr(): real_max_P(0), max_S(0), max_P(0), max_O(0), nTriples(0) {}

	ring_rpq_attr(std::string filename)
	{
		construct(filename);
	}



	void construct(std::string filename, bool verbose = false)
	{
		ifstream ifs(filename.c_str());
		if (!ifs.is_open()) throw std::invalid_argument("Could not open " + filename);
		if (verbose) cout << "Opened " << filename << " successfully" << endl;
		
		auto start = timer::now();

		// Read attribute names and types, 
		// and count how many attributes there are
		if (verbose) cout << "preprocessing attributes..." << endl;
		std::vector<AttrInfo> edge_attr_infos;
		std::vector<AttrInfo> node_attr_infos;
		auto attr_counts = load_attribute_info(filename, A_name_to_info, &edge_attr_infos, &node_attr_infos);
		int n_of_str_attrs = get<0>(attr_counts); 
		int n_of_int_attrs = get<1>(attr_counts); 
		int n_of_flt_attrs = get<2>(attr_counts); 

		// initialize maps for storing attributes values
		std::vector<std::unordered_map<uint64_t, std::string>> _str_attributes(n_of_str_attrs);
		std::vector<std::unordered_map<uint64_t, int>> _int_attributes(n_of_int_attrs);
		std::vector<std::unordered_map<uint64_t, float>> _flt_attributes(n_of_flt_attrs);

		// Gather the triples into a vector Data, and read edge attributes

		// (the fourth uint64_t below is the triple's index, comparable to a edge id)
		using spo_triple_with_id = tuple<uint64_t, uint64_t, uint64_t, uint64_t>;
		vector<spo_triple_with_id> Data;

		// find largest predicate
		ifstream P_ifs((filename + ".P").c_str());
		uint64_t max_p = 0;
		uint64_t p;
		while (P_ifs >> p)
		{
			if (p > max_p) max_p = p;
			P_ifs.ignore(numeric_limits<streamsize>::max(), '\n');
		}
		real_max_P = max_p + 1;
		max_P = max_p + 1;

		cout << "reading triples..." << endl;
		uint64_t s, o;
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
						_str_attributes.at(attr.id)[triple_id] = str_value;
					}
					else if (attr.type == AttrInfo::Int)
					{
						_int_attributes.at(attr.id)[triple_id] = stoi(str_value);
					}
					else if (attr.type == AttrInfo::Flt)
					{
						_flt_attributes.at(attr.id)[triple_id] = stof(str_value);
					}
				} 
			}
			// add the triple to Data
			Data.push_back(spo_triple_with_id(s, p + real_max_P, o, triple_id));
			Data.push_back(spo_triple_with_id(o, p, s, triple_id));
			triple_id++;

			if (p + real_max_P > max_P) max_P = p + real_max_P;
			if (s > max_S) max_S = s;
			if (o > max_O) max_O = o;
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
						_str_attributes.at(attr.id)[triple_id] = str_value;
					}
					else if (attr.type == AttrInfo::Int)
					{
						_int_attributes.at(attr.id)[triple_id] = stoi(str_value);
					}
					else if (attr.type == AttrInfo::Flt)
					{
						_flt_attributes.at(attr.id)[triple_id] = stof(str_value);
					}
				}
			}
			// add a reflexive edge to store node attributes, with an unused predicate id
			Data.push_back(spo_triple_with_id(node_id, max_p + 1, node_id, triple_id));
			triple_id++;
		}

		Data.shrink_to_fit();
		
		if (verbose)
		{
			cout << "--Indexing " << Data.size() << " triples " 
			     << "with " << A_name_to_info.size() << " attributes "
			     << "(str: "<< n_of_str_attrs 
			     << ", int: " << n_of_int_attrs 
			     << ", flt: " << n_of_flt_attrs << ")" << endl;
		}
		auto data_end = timer::now();

		nTriples = Data.end() - Data.begin();

		if (verbose) cout << "  > Determining number of elements per symbol";
		fflush(stdout);
		uint64_t alphabet_SO = (max_S < max_O) ? max_O : max_S;

		std::vector<uint32_t> M_O(alphabet_SO + 1, 0), M_P(max_P + 1, 0);

		for (uint64_t i = 0; i < Data.size(); i++) {
			M_O[std::get<2>(Data[i])]++;
			M_P[std::get<1>(Data[i])]++;
		}

		M_O.shrink_to_fit();
		M_P.shrink_to_fit();

		auto sort1_start = timer::now();

		// Sort the triples in OSP order
		if (verbose) cout << "... [done]\n  > Sorting out triples in OSP order" << flush;
		sort(Data.begin(), Data.end(), [](const spo_triple_with_id& a,
		                                  const spo_triple_with_id& b) 
				{ return std::tie(std::get<2>(a), std::get<0>(a), std::get<1>(a))
		               < std::tie(std::get<2>(b), std::get<0>(b), std::get<1>(b));});
		if (verbose) cout << "... [done]" << endl;

		auto sort1_stop = timer::now();

		//Building BWT_P

		{
			vector<uint64_t> new_C_P;
			if (verbose) cout << "  > Building C_P";
			uint64_t cur_pos = 1;
			new_C_P.push_back(0);  // Dummy value
			new_C_P.push_back(cur_pos);
			for (uint64_t c = 2; c <= alphabet_SO; c++) {
				cur_pos += M_O[c-1];
				new_C_P.push_back(cur_pos);
			}
			new_C_P.push_back(nTriples + 1);
			new_C_P.shrink_to_fit();
			M_O.clear();
			if (verbose) cout << "... [done]" << endl;
		   
			if (verbose) cout << "  > Building bwtp" << flush;
			int_vector<> new_P(nTriples + 1);
			new_P[0] = 0;
			for (uint64_t i = 1; i <= nTriples; i++)
				new_P[i] = std::get<1>(Data[i-1]);
			util::bit_compress(new_P);

			L_P = bwt(new_P, new_C_P);
		}


		auto sort2_start = timer::now();

		if (verbose) cout << "... [done]\n  > Stable sorting out triples in POS order" << flush;
		stable_sort(Data.begin(), Data.end(), [](const spo_triple_with_id& a,
		                                         const spo_triple_with_id& b) 
				{return std::get<1>(a) < std::get<1>(b);});
		if (verbose) cout << "... [done]" << endl;

		auto sort2_stop = timer::now();

		//Building BWT_S
		{
			if (verbose) cout << "  > Building C_S";
			uint64_t cur_pos = 1;
			C_S.push_back(0);  // Dummy value
			C_S.push_back(cur_pos);
			for (uint64_t c = 2; c <= max_P; c++) {
				cur_pos += M_P[c-1];
				C_S.push_back(cur_pos);
			}
			C_S.push_back(nTriples + 1);
			C_S.shrink_to_fit();
			M_P.clear();
			if (verbose) cout << "... [done]" << endl;

			if (verbose) cout << "  > Building s vector and edge attributes" << flush;
			// initialize attribute vectors with empty maps
			for (unsigned int a = 0; a < _str_attributes.size(); a++) str_attributes.emplace_back();
			for (unsigned int a = 0; a < _int_attributes.size(); a++) int_attributes.emplace_back();
			for (unsigned int a = 0; a < _flt_attributes.size(); a++) flt_attributes.emplace_back();
			// Copy attribute values given as constructor parameter (_str_attributes etc.)
			// into class member variables (str_attributes etc.),
			// permuting them to the POS-sorted order.
			// (also construct L_S at the same time)
			L_S.resize(nTriples + 1);
			L_S[0] = 0;
			uint64_t triple_id = 0;
			for (uint64_t i = 1; i <= nTriples; i++)
			{
				L_S[i] = std::get<0>(Data[i-1]);
				triple_id = std::get<3>(Data[i-1]);
				for (unsigned int a = 0; a < _str_attributes.size(); a++)
				{
					if (_str_attributes.at(a).find(triple_id) != _str_attributes.at(a).end())
					{
						str_attributes.at(a)[i] = _str_attributes.at(a).at(triple_id);
					}
				}
				for (unsigned int a = 0; a < _int_attributes.size(); a++)
				{		
					if (_int_attributes.at(a).find(triple_id) != _int_attributes.at(a).end())
					{
						int_attributes.at(a)[i] = _int_attributes.at(a).at(triple_id);
					}
				}
				for (unsigned int a = 0; a < _flt_attributes.size(); a++)
				{		
					if (_flt_attributes.at(a).find(triple_id) != _flt_attributes.at(a).end())
					{
						flt_attributes.at(a)[i] = _flt_attributes.at(a).at(triple_id);
					}
				}
			}
			util::bit_compress(L_S);
			if (verbose) cout << "... [done]" << endl;
		}

		B_array = std::vector<word_t>(4 * max_P, 0);

		auto stop = timer::now();

		if (verbose) cout << "-- Index constructed successfully" << endl;
		if (verbose)
		{
			int data_time = duration_cast<seconds>(data_end - start).count();
			int initial_time = duration_cast<seconds>(sort1_start - data_end).count();
			int sort1_time = duration_cast<seconds>(sort1_stop - sort1_start).count();
			int construct1_time = duration_cast<seconds>(sort2_start - sort1_stop).count();
			int sort2_time = duration_cast<seconds>(sort2_stop - sort2_start).count();
			int construct2_time = duration_cast<seconds>(stop - sort2_stop).count();
			printf("%d, %d, %d, %d, %d, %d\n", data_time, initial_time,
			                                   sort1_time, construct1_time, 
			                                   sort2_time, construct2_time);
		}

	};



	// Size of the index in bytes
	// (not completely accurate due to the attributes)
	uint64_t size() {
		//cout << "L_S.size() = " << L_S.size() << " bytes" << endl;
		//cout << "L_P.size() = " << L_P.size() << " bytes" << endl;
		//cout << "nTriples = " << nTriples << endl;
		unsigned int attrs_size = 0;
		for (unsigned int a = 0; a < str_attributes.size(); a++)
			attrs_size += str_attributes.at(a).size() * sizeof(std::string);
		for (unsigned int a = 0; a < int_attributes.size(); a++)
			attrs_size += int_attributes.at(a).size() * sizeof(uint64_t);
		for (unsigned int a = 0; a < flt_attributes.size(); a++)
			attrs_size += flt_attributes.at(a).size() * sizeof(float);
		return L_S.size() + L_P.size() + C_S.capacity() * sizeof(uint64_t) + attrs_size;
	}

	uint64_t n_triples() {
		return nTriples;
	}

	uint64_t n_labels() {
		return max_P;
	}



	void save(string filename) {
		//L_S.save(filename + ".bwts");
		sdsl::store_to_file(L_S, filename + ".L_S");
		
		std::ofstream fp_C(filename + ".C_S");
		for (uint64_t i = 0; i < C_S.size(); i++)
		{
			fp_C << C_S[i] << endl;
		}

		std::ofstream fp_attr_str(filename + ".attr.str");
		for (unsigned int a = 0; a < str_attributes.size(); a++)
		{
			fp_attr_str << str_attributes.at(a).size() << endl;
			for (auto pair : str_attributes.at(a))
			{
				fp_attr_str << pair.first << " " << pair.second << endl;
			}
		}
		std::ofstream fp_attr_int(filename + ".attr.int");
		for (unsigned int a = 0; a < int_attributes.size(); a++)
		{
			fp_attr_int << int_attributes.at(a).size() << endl;
			for (auto pair : int_attributes.at(a))
			{
				fp_attr_int << pair.first << " " << pair.second << endl;
			}
		}
		std::ofstream fp_attr_flt(filename + ".attr.flt");
		for (unsigned int a = 0; a < flt_attributes.size(); a++)
		{
			fp_attr_flt << flt_attributes.at(a).size() << endl;
			for (auto pair : flt_attributes.at(a))
			{
				fp_attr_flt << pair.first << " " << pair.second << endl;
			}
		}

		L_P.save(filename + ".bwtp");

		std::ofstream ofs(filename + ".nTriples");
		ofs << nTriples << endl;
		ofs << real_max_P << endl;
		ofs << max_S << endl;
		ofs << max_P << endl;
		ofs << max_O << endl;
	};



	void load(string filename)
	{	
		// Open input files
		ifstream ifs_SO(filename + ".SO", ifstream::in);
		if (!ifs_SO.is_open()) throw invalid_argument("Could not open" + filename + ".SO");
		ifstream ifs_P(filename + ".P", ifstream::in);
		if (!ifs_P.is_open()) throw invalid_argument("Could not open" + filename + ".P");
		cout << "Opened file " << filename << " successfully" << endl;

		// ensure index is empty before loading
		C_S.clear();
		str_attributes.clear();
		int_attributes.clear();
		flt_attributes.clear();
		P_id_to_name.clear();
		P_name_to_id.clear();
		SO_id_to_name.clear();
		SO_name_to_id.clear();
		A_name_to_info.clear();
		B_array.clear();

		// Get attribute info (id, Edge/Node, Str/Int/Flt)
		load_attribute_info(filename, A_name_to_info);

		cout << "Loading L_S" << endl; fflush(stdout);
		sdsl::load_from_file(L_S, filename + ".L_S");
		{
		 std::ifstream ifs_C_S(filename + ".C_S");
		 uint64_t c;
		 while (ifs_C_S >> c) { C_S.push_back(c); }
		}
		
		cout << "Loading attr.str" << endl; fflush(stdout);
		{
		 std::ifstream ifs_attr_str(filename + ".attr.str");
		 unsigned int map_size;
		 uint64_t key;
		 std::string value;
		 while (ifs_attr_str >> map_size)
		 {
			str_attributes.emplace_back(std::unordered_map<uint64_t, std::string>());
			for (unsigned int i = 0; i < map_size; i++)
			{
				ifs_attr_str >> key >> value;
				str_attributes.back()[key] = value;
			}
		 }
		}		
		cout << "Loading attr.int" << endl; fflush(stdout);
		{
		 std::ifstream ifs_attr_int(filename + ".attr.int");
		 unsigned int map_size;
		 uint64_t key;
		 int value;
		 while (ifs_attr_int >> map_size)
		 {
			int_attributes.emplace_back(std::unordered_map<uint64_t, int>());
			for (unsigned int i = 0; i < map_size; i++)
			{
				ifs_attr_int >> key >> value;
				int_attributes.back()[key] = value;
			}
		 }
		}
		cout << "Loading attr.flt" << endl; fflush(stdout);
		{
		 std::ifstream ifs_attr_flt(filename + ".attr.flt");
		 unsigned int map_size;
		 uint64_t key;
		 float value;
		 while (ifs_attr_flt >> map_size)
		 {
			flt_attributes.emplace_back(std::unordered_map<uint64_t, float>());
			for (unsigned int i = 0; i < map_size; i++)
			{
				ifs_attr_flt >> key >> value;
				flt_attributes.back()[key] = value;
			}
		 }
		}

		cout << "Loading L_P" << endl; fflush(stdout);
		L_P.load(filename + ".bwtp");
		cout << "Loading done" << endl; fflush(stdout);
		std::ifstream ifs(filename + ".nTriples");
		ifs >> nTriples;
		ifs >> real_max_P;
		ifs >> max_S;
		ifs >> max_P;
		ifs >> max_O;

		B_array = std::vector<word_t>(4 * max_P, 0);


		cout << "Loading node names" << endl; fflush(stdout);
		SO_name_to_id.reserve(std::max(max_S, max_O));
		SO_id_to_name.reserve(std::max(max_S, max_O));
		uint64_t id;
		string name;
		while (ifs_SO >> id >> name)
		{
			SO_name_to_id[name] = id;
			SO_id_to_name[id] = name;
			ifs_SO.ignore(numeric_limits<streamsize>::max(), '\n');
		}
		cout << "Loading predicate names" << endl; fflush(stdout);
		P_name_to_id.reserve(real_max_P);
		P_id_to_name.reserve(real_max_P);
		uint64_t max_p = 0;
		while (ifs_P >> id >> name)
		{
			if (id == 0) throw std::invalid_argument("predicate ids must nonzero");
			P_name_to_id[name] = id;
			P_id_to_name[id] = name;
			if (id > max_p) max_p = id;
			ifs_P.ignore(numeric_limits<streamsize>::max(), '\n');
		}
		// give the node symbol an unused id
		P_name_to_id[NODE_PREDICATE] = max_p + 1;
		P_id_to_name[max_p + 1] = NODE_PREDICATE;
		// give the wildcard edge an unused id
		P_name_to_id[WILDCARD_PREDICATE] = 0;
		P_id_to_name[0] = WILDCARD_PREDICATE;
	};

	uint64_t pred_selectivity(uint64_t pred_id) {
		return C_S[pred_id + 1] - C_S[pred_id];
	};


	inline uint64_t pred_reverse(uint64_t pred_id) const{
		return (pred_id > real_max_P) ? pred_id - real_max_P : pred_id + real_max_P;
	}


	// Evaluates a query, collecting in output the pairs of names of nodes
	// that are connected by a path that matches the query.
	// The query should consist of subject name, rpq expression, and object name,
	// where the subject/object can be replaced with a question mark.
	// (More detailed syntax information is in this repository's README file.)
	// If bound is given, searches and returns only up to that many results.
	// Quits early if the given timeout is exceeded.
	ParseError evaluate(std::string query_line, 
	                    std::vector<std::pair<std::string, std::string>>& output,
	                    uint64_t bound = std::numeric_limits<uint64_t>::max(),
	                    unsigned int timeout = TIME_OUT)
	{
		unordered_map<string, uint64_t> pred_map;
		pred_map[NODE_PREDICATE] = P_name_to_id.at(NODE_PREDICATE);
		unordered_map<string, AttrInfo> attr_map;
		pred_map[WILDCARD_PREDICATE] = P_name_to_id.at(WILDCARD_PREDICATE);
		std::string rpq, query, query_type;
		vector<uint64_t> pred_vec;
		uint64_t s_id = 0;
		uint64_t o_id = 0;
		vector<string> subqueries;

		// separate subject, rpq and object from the query line
		tie(s_id, rpq, o_id) = parse_subject_and_object(query_line, SO_name_to_id);
		if (rpq == "")
		{
			return ParseError::MissingEndpoint;
		}

		// extract possible branching subqueries into separate queries
		rpq = extract_nested_subqueries(rpq, subqueries);
		if (rpq == "")
		{
			return ParseError::InvalidCurlyBrackets;
		}

		// evaluate the subqueries (if any)
		prepare_nested_query(attr_map, subqueries.size());
		for (unsigned int sub_q = 0; sub_q < subqueries.size(); sub_q++)
		{
			// parse
			ParseError error = parse_query(subqueries.at(sub_q), query, query_type, pred_vec,
			                               P_name_to_id, pred_map, A_name_to_info, attr_map);
			if (error != ParseError::None)
			{
				return error;
			}

			// evaluate (results are stored internally)
			bool is_trivial = run_subquery(sub_q, query, pred_map, attr_map);
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
		                               P_name_to_id, pred_map, A_name_to_info, attr_map);
		if (error != ParseError::None)
		{
			return error;
		}
		run_query(query, query_type, pred_vec, s_id, o_id, pred_map, attr_map, output, bound);
		cleanup_nested_query(subqueries.size());

		return ParseError::None;
	}



private:

	// mark in the wavelet tree L_P 
	// which automaton states are potentially useful for which ranges of predicates 
	void mark(RpqAutomataAttr& A)
	{
		const std::unordered_map<uint64_t, uint64_t>& m = A.getB();
		for (auto it = m.begin(); it != m.end(); it++) 
		{
			L_P.mark<word_t>(it->first, B_array, (word_t) it->second);
		}
	}

	// clean the markings
	void unmark(RpqAutomataAttr& A)
	{
		const std::unordered_map<uint64_t, uint64_t>& m = A.getB();
		for (auto it = m.begin(); it != m.end(); it++) 
		{
			L_P.unmark<word_t>(it->first, B_array);
		}
	}


	inline interval_state_type& last_element(std::stack<interval_state_type> &stack){
		return stack.top();
	}


	inline interval_state_type first_element(std::stack<interval_state_type> &stack){
		return stack.top();
	}


	inline interval_state_type& last_element(std::queue<interval_state_type> &queue){
		return queue.back();
	}

	inline interval_state_type first_element(std::queue<interval_state_type> &queue){
		return queue.front();
	}


	// Like next_step (below), but uses all available predicates.
	void next_step_any(RpqAutomataAttr& A,
	                   initializable_array<word_t>& D_array,
	                   word_t current_D, bwt_interval& I_p,
	                   Container& ist_container,
	                   std::function<bool(uint64_t)> outputter)
	{
		// Finding all predicates in the object's interval I_p
		std::vector<uint64_t> pred_vec;
		L_P.all_values_in_range_bounded(I_p.left(), I_p.right(), pred_vec, I_p.size());
	
		uint64_t c;
		std::pair<uint64_t, uint64_t> I_s;
		for (uint64_t i = 0; i < pred_vec.size(); i++)
		{
			uint64_t& p = pred_vec[i];
			// get interval of subjects that are reached with p
			I_s = L_P.backward_step(I_p.left(), I_p.right(), p);
			c = C_S[p];
			I_s.first += c;
			I_s.second += c;

			// Go through all triples in the interval in L_S,
			// check if their attributes are accepted by the automaton,
			// and add them to the next step's objects if needed.
			word_t actual_current_D;
			word_t new_D;
			word_t actual_new_D;
			uint64_t s;
			for (uint64_t j = I_s.first; j <= I_s.second; j++)
			{
				s = L_S[j];

				// check attributes
				actual_current_D = current_D & A.states_with_matching_attributes(j, p,
					                                str_attributes, int_attributes, flt_attributes);

				new_D = A.next(actual_current_D, p, BWD);
				actual_new_D = new_D & ~D_array.atPos(s); // prune already visited states
				D_array[s] = D_array.atPos(s) | actual_new_D; // mark the states as visited
				
				if (A.atFinal(actual_new_D, BWD))
				{
					// new result
					bool stop = outputter(j);
					if (stop) return; // bound for results was exceeded
				}

				// enqueue the subject's interval in L_P for the next step
				push_merge_interval(ist_container, s, actual_new_D);
			}
		}
	}



	// Add to ist_container the ranges of nodes that are reached in one step
	// from the interval I_p and automaton states current_D.
	// If new results to the query are found, output them using the given lambda function.
	void next_step(RpqAutomataAttr& A,
	               initializable_array<word_t>& D_array,
	               word_t current_D, bwt_interval& I_p,
	               Container& ist_container,
	               const std::function<bool(uint64_t)>& outputter)
	{
		if (A.anyActive(current_D))
		{
			next_step_any(A, D_array, current_D, I_p, ist_container, outputter);
			return;
		}

		// Finding predicates from the object whose interval in L_p is I_p,
		// where, for each predicate, the values in L_S are obtained by using a backward step.
		// These predicates and intervals in L_S are collected to pred_vec.
		std::vector<std::pair<uint64_t, std::pair<uint64_t, uint64_t>>> pred_vec;
		L_P.all_active_p_values_in_range_test<word_t>(I_p.left(), I_p.right(),
		                                              B_array, current_D, pred_vec);
	
		uint64_t c;
		for (uint64_t i = 0; i < pred_vec.size(); i++)
		{
			uint64_t& p = pred_vec[i].first;
			uint64_t& interval_left = pred_vec[i].second.first;
			uint64_t& interval_right = pred_vec[i].second.second;
			c = C_S[p];

			// Go through all triples in the interval in L_S,
			// check if their attributes are accepted by the automaton,
			// and add them to the next step's objects if needed.
			word_t actual_current_D;
			word_t new_D;
			word_t actual_new_D;
			uint64_t s;
			for (uint64_t j = c + interval_left; j < c + interval_right; j++)
			{
				s = L_S[j];

				// check attributes
				actual_current_D = current_D & A.states_with_matching_attributes(j, p,
					                                str_attributes, int_attributes, flt_attributes);

				new_D = A.next(actual_current_D, p, BWD);
				actual_new_D = new_D & ~D_array.atPos(s); // prune already visited states
				D_array[s] = D_array.atPos(s) | actual_new_D; // mark the states as visited
				
				if (A.atFinal(actual_new_D, BWD))
				{
					// new result
					bool stop = outputter(j);
					if (stop) return; // bound for results was exceeded
				}

				// enqueue the subject's interval in L_P for the next step
				push_merge_interval(ist_container, s, actual_new_D);
			}
		}
	}


	// Checks whether the query's attribute constraints are fulfilled 
	// by the triple at index i in POS order.
	// Parameter acceptor_i is the constraints' position in the query.
	bool has_matching_attributes(uint64_t i, int acceptor_i,
	                             const vector<vector<pair<AttrInfo, Acceptor>>>& attr_acceptors)
	{
		// check that all constraints in attr_acceptors.at(acceptor_i) are fulfilled
		for (auto attr_and_acceptor : attr_acceptors.at(acceptor_i))
		{
			const AttrInfo& attr = attr_and_acceptor.first;
			const Acceptor& acceptor = attr_and_acceptor.second;

			if (attr.type == AttrInfo::Str)
			{
				if (str_attributes.at(attr.id).find(i) != str_attributes.at(attr.id).end())
				{
					// attribute value exists, test whether it is accepted
					const std::string& value = str_attributes.at(attr.id).at(i);
					if (!acceptor(value)) return false;
				}
				// attribute value is missing, test whether null is accepted
				else if (!acceptor()) return false;
			}
			else if (attr.type == AttrInfo::Int)
			{
				if (int_attributes.at(attr.id).find(i) != int_attributes.at(attr.id).end())
				{
					int value = int_attributes.at(attr.id).at(i);
					if (!acceptor(value)) return false;
				}
				else if (!acceptor()) return false;
			}
			else if (attr.type == AttrInfo::Flt)
			{
				if (flt_attributes.at(attr.id).find(i) != flt_attributes.at(attr.id).end())
				{
					float value = flt_attributes.at(attr.id).at(i);
					if (!acceptor(value)) return false;
				}
				else if (!acceptor()) return false;
			}
		}

		return true;
	}


	// Add the given symbol's interval in L_P and the given nfa_states
	// to ist_container, merging adjacent compatible intervals.
	void push_merge_interval(Container& ist_container,
							 const uint64_t& symbol, const word_t& nfa_states)
	{

		const auto lb = L_P.get_C(symbol);
		const auto rb = L_P.get_C(symbol + 1) - 1;

		if (ist_container.empty())
		{
			ist_container.push(interval_state_type{bwt_interval(lb, rb), nfa_states});
			return;
		}

		auto& last = last_element(ist_container);
		if (last.interval.right()+1 == lb && last.current_D == nfa_states)
		{
			//The previous interval is contiguous and both have equal NFA state
			last.interval.set_right(rb);
		}
		else
		{
			ist_container.push(interval_state_type{bwt_interval(lb, rb), nfa_states});
		}
	}






	// Collects in object_vector the endpoint of each
	// path accepted by the RPQ-automaton A.
	// Returns true without collecting objects if the query accepts empty paths. 
	// Assumes that B_array has been marked in L_P.
	bool rpq_var_to_var_obtain_o(RpqAutomataAttr &A,
	                             std::vector<uint64_t> &object_vector,
	                             timer::time_point start,
	                             uint64_t bound = std::numeric_limits<uint64_t>::max())
	{
		timer::time_point stop;
		double total_time = 0.0;
		duration<double> time_span;

		initializable_array<word_t> D_array(4 * (max_O + 1), 0);

		bool time_out = false;

		word_t current_D = (word_t) A.getFinalStates();
		if (A.atFinal(current_D, BWD)) return true;

		// create a lambda that is used to add new results
		std::function<bool(uint64_t)> outputter = [this, &object_vector, bound](uint64_t i)
		{
			object_vector.push_back(L_S[i]);
			return (object_vector.size() >= bound);
		};

		// Search the graph in a BFS/DFS-like fashion:

		Container ist_container; //contains intervals with NFA states
		ist_container.push(interval_state_type{bwt_interval(1, nTriples), current_D});
		while (!time_out && !ist_container.empty()) 
		{
			// pop queue/stack (depends on class template argument)
			auto ist_top = first_element(ist_container);
			ist_container.pop();
			// advance
			next_step(A, D_array, ist_top.current_D, ist_top.interval, ist_container, outputter);
			// quit early if reached bound for results
			if (object_vector.size() >= bound) return false;
			// check for timeout
			stop = timer::now();
			time_span = duration_cast<microseconds>(stop - start);
			total_time = time_span.count();
			if (total_time > TIME_OUT) time_out = true;
		}
		return false;
	};


	// Like rpq_var_to_var_obtain_o (above),
	// but results are marked into the auxiliary variable identified by q_attr
	// instead of collecting them to a vector.
	bool rpq_subquery_obtain_o(int q_attr, RpqAutomataAttr &A,
	                           timer::time_point start)
	{
		timer::time_point stop;
		double total_time = 0.0;
		duration<double> time_span;

		initializable_array<word_t> D_array(4 * (max_O + 1), 0);
		
		bool time_out = false;

		word_t current_D = (word_t) A.getFinalStates();
		if (A.atFinal(current_D, BWD)) return true;

		// create a lambda that is used to add new results
		std::function<bool(uint64_t)> outputter = [this, q_attr](uint64_t i)
		{
			int_attributes.at(q_attr)[i] = 1;
			return false;
		};

		Container ist_container; // contains intervals with NFA states
		ist_container.push(interval_state_type{bwt_interval(1, nTriples), current_D});
		while (!time_out && !ist_container.empty()) 
		{
			// pop queue/stack (depending on class template argument)
			auto ist_top = first_element(ist_container);
			ist_container.pop();
			// advance
			next_step(A, D_array, ist_top.current_D, ist_top.interval, ist_container, outputter);
			// check for timeout
			stop = timer::now();
			time_span = duration_cast<microseconds>(stop - start);
			total_time = time_span.count();
			if (total_time > TIME_OUT) time_out = true;
		}
		return false;
	};


	// Searches the RPQ represented by automaton A, starting from initial_object.
	// Collects result path endpoint pairs in the solutions vector.
	// Returns true iff a timeout occurs.
	bool _rpq_const_s_to_var_o(RpqAutomataAttr &A,
	                           uint64_t initial_object,
	                           std::vector<std::pair<std::string, std::string>> &solutions,
	                           bool const_to_var,
	                           timer::time_point start,
	                           uint64_t bound = std::numeric_limits<uint64_t>::max())
	{
		timer::time_point stop;
		double total_time = 0.0;
		duration<double> time_span;

		word_t current_D;  // palabra de maquina D, con los estados activos
		initializable_array<word_t> D_array(4 * (max_O + 1), 0);

		Container ist_container; //contains intervals with NFA states
		current_D = (word_t) A.getFinalStates();
		ist_container.push(interval_state_type{bwt_interval(L_P.get_C(initial_object),
		                                       L_P.get_C(initial_object + 1) - 1), current_D});
		
		if (A.atFinal(current_D, BWD)) // are empty paths accepted?
		{
			// add solution
			std::string name = SO_id_to_name.at(initial_object);
			solutions.emplace_back(name, name);
			// mark initial states as visited
			D_array[initial_object] = D_array.atPos(initial_object) | current_D;
		}

		// create a lambda that is used to add new results
		std::function<bool(uint64_t)> outputter = [this, &solutions, const_to_var, 
		                                           initial_object, bound](uint64_t i)
		{
			std::string name1 = SO_id_to_name.at(initial_object);
			std::string name2 = SO_id_to_name.at(L_S[i]);
			if (const_to_var) solutions.emplace_back(name1, name2);
			else              solutions.emplace_back(name2, name1);
			return (solutions.size() >= bound);
		};

		// Search the graph in a BFS/DFS-like fashion: (depending on class template argument)
		while (!ist_container.empty()) 
		{
			auto ist_top = first_element(ist_container);
			ist_container.pop();
			next_step(A, D_array, ist_top.current_D, ist_top.interval, ist_container, outputter);
			
			if (solutions.size() >= bound) return false;
			stop = timer::now();
			time_span = duration_cast<microseconds>(stop - start);
			total_time = time_span.count();
			if (total_time > TIME_OUT) return true;
		}
		return false;
	};


	// Returns true iff a path accepted by A exists between s and o.
	bool _rpq_const_to_const(RpqAutomataAttr &A,
	                         uint64_t s, uint64_t o,
	                         high_resolution_clock::time_point start)
	{
		// in the case of const-to-const queries, 
		// there result is simply that there either is a path or not
		bool path_exists = false;

		initializable_array<word_t> D_array(4 * (max_O + 1), 0);

		word_t current_D = (word_t) A.getFinalStates();
		if (A.atFinal(current_D, BWD)) // are empty paths accepted?
		{
			if (s == o) return true;
			// mark initial states as visited
			D_array[o] = D_array.atPos(o) | current_D;
		}

		std::function<bool(uint64_t)> outputter = [this, s, &path_exists](uint64_t i)
		{
			if (s == L_S[i]) path_exists = true;
			return path_exists;
		};

		// Search the graph in a BFS/DFS-like fashion:

		high_resolution_clock::time_point stop;
		double total_time = 0.0;
		bool time_out = false;
		Container ist_container; //contains intervals with NFA states
		auto start_interval = bwt_interval(L_P.get_C(o), L_P.get_C(o + 1) - 1);
		ist_container.push(interval_state_type{start_interval, current_D});
		while (!time_out && !ist_container.empty()) 
		{
			// pop queue/stack (depends on class template argument)
			auto ist_top = first_element(ist_container);
			ist_container.pop();
			// advance
			next_step(A, D_array, ist_top.current_D, ist_top.interval, ist_container, outputter);
			// quit if found result
			if (path_exists) return true;
			// check for timeout
			stop = high_resolution_clock::now();
			total_time = duration_cast<microseconds>(stop - start).count();
			if (total_time > TIME_OUT) time_out = true;
		}
		return false;
	};





	// Solves unions of single edges (e.g. <a>|<b>|<c>)
	void or_query_var_to_var(const std::string &input_rpq, uint64_t n_or,
	                          unordered_map<std::string, uint64_t> &predicates_map,
	                          unordered_map<std::string, AttrInfo> &attributes_map,
	                          std::vector<std::pair<std::string, std::string>> &output,
	                          uint64_t bound = std::numeric_limits<uint64_t>::max())
	{
		// remove attribute constraints and create acceptors from them
		vector<vector<pair<AttrInfo, Acceptor>>> attr_acceptors;
		std::string rpq = parse_attribute_conditions(input_rpq, attr_acceptors, attributes_map);

		auto start_time = timer::now();

		// First, for each predicate in the query, find the predicate id and interval in I_S:
		std::vector<int64_t> pred_v;
		std::vector<bool> negated_pred_v;
		std::vector<std::pair<uint64_t, uint64_t>> I_S_v;
		uint64_t i = 0; // index within string rpq
		for (uint64_t k = 0; k < n_or; k++) 
		{
			negated_pred_v.push_back(rpq.at(i + 1) == '%'); // is the predicate reversed?
			uint64_t start = i; // predicate name start index
			while (rpq.at(i) != '>') ++i; // skip to end of predicate name

			// find predicate id and add it to pred_v
			if (negated_pred_v[k])
				pred_v.push_back(predicates_map["<" + rpq.substr(start + 2, i-start-1)]);
			else
				pred_v.push_back(predicates_map[rpq.substr(start, i-start-1 + 2)]);

			// find the interval and add it to I_S_v
			I_S_v.push_back(std::pair<uint64_t, uint64_t>(
					C_S[pred_v[k] + real_max_P],
					C_S[pred_v[k] + real_max_P + 1] - 1
			));
			i += 2; // skip operator '|'
		}

		// Evaluate each predicate like a single predicate query, 
		// starting with the ones that return most results:

		// initialize clock
		auto stop = timer::now();
		auto total_time = duration_cast<seconds>(stop - start_time).count();
		bool time_out = (total_time > TIME_OUT);

		// set to keep track of duplicate results
		std::unordered_set<std::pair<uint64_t, uint64_t>> result_set;
		std::pair<std::unordered_set<std::pair<uint64_t, uint64_t>>::iterator, bool> insert_info;

		uint64_t pred, negated_pred, a, max_pos, c, object;
		std::pair<uint64_t, uint64_t> I_S;
		for (uint64_t k = 0; !time_out && k < n_or; k++) 
		{
			// find predicate that has most objects
			for (a = 0; a < n_or and pred_v[a] == -1; ++a);
			max_pos = a;
			for (++a; a < n_or; ++a) 
			{
				if (pred_v[a] == -1) continue;
				if (I_S_v[a].second - I_S_v[a].first > I_S_v[max_pos].second - I_S_v[max_pos].first)
					max_pos = a;
			}

			pred = pred_v[max_pos];
			uint64_t interval_left = I_S_v[max_pos].first;
			uint64_t interval_right = I_S_v[max_pos].second;
			negated_pred = negated_pred_v[max_pos];
			pred_v[max_pos] = -1;
		

			for (uint64_t i = interval_left; !time_out && i <= interval_right; i++) 
			{
				object = L_S[i];
				I_S = L_P.backward_step(L_P.get_C(object), L_P.get_C(object + 1) - 1, pred);
				c = C_S[pred];
				I_S.first += c;
				I_S.second += c;
				for (uint64_t j = I_S.first; !time_out && j <= I_S.second; j++)
				{
					if (!has_matching_attributes(j, max_pos, attr_acceptors)) continue;
					
					if (negated_pred) insert_info = result_set.emplace(L_S[j], object);
					else              insert_info = result_set.emplace(object, L_S[j]);
					
					if (insert_info.second)
					{
						// wasn't yet in result_set, no risk of duplicate result
						const auto& id_pair = *insert_info.first;
						std::string name1 = SO_id_to_name.at(id_pair.first);
						std::string name2 = SO_id_to_name.at(id_pair.second);
						output.emplace_back(name1, name2);
						if (output.size() >= bound) return;
					}
					stop = timer::now();
					total_time = duration_cast<seconds>(stop - start_time).count();
					time_out = (total_time > TIME_OUT);
				}
			}

		}
	};

    // Solves single-predicate queries (i.e. no operators)
	void single_predicate_query(uint64_t p, bool is_reversed, uint64_t s, uint64_t o, 
	                            unordered_map<std::string, AttrInfo>& attributes_map,
	                            vector<vector<pair<AttrInfo, Acceptor>>>& attr_acceptors,
	                            std::vector<std::pair<std::string, std::string>>& output,
	                            uint64_t bound = std::numeric_limits<uint64_t>::max())
	{
		// initialize clock, to check for timeout
		auto start = timer::now();
		auto stop = start;
		int total_time = 0;
		bool time_out = false;

		// find the actual predicate id:
		// p is the id of the predicate as found in the query,
		// p_rev is the reverse of that.
		uint64_t p_rev = 0;
		if (p == real_max_P) // reflexive node edge
		{
			p_rev = p;
		}
		else if (!is_reversed) 
		{
			p_rev = p;
			p += real_max_P;
		}
		else
		{
		   p_rev = p + real_max_P; 
		}

		std::unordered_set<uint64_t> result_set; // to keep track of duplicates
		
		if (s == 0 and o != 0) // var to const
		{
			// find interval of subjects from which there is a p-labeled edge to o
			auto I_S = L_P.backward_step(L_P.get_C(o), L_P.get_C(o+1)-1, p);
			uint64_t c = C_S[p];
			I_S.first += c;
			I_S.second += c;

			// for each subject, add it to output if attributes match
			uint64_t subj = 0;
			for (uint64_t i = I_S.first; !time_out && i <= I_S.second; i++)
			{
				// check attributes
				if (!has_matching_attributes(i, 0, attr_acceptors)) continue;

				subj = L_S[i];
				// add result if not duplicate
				auto iter = result_set.find(subj);
				if (iter == result_set.end())
				{
					std::string name1 = SO_id_to_name.at(subj);
					std::string name2 = SO_id_to_name.at(o);
					output.emplace_back(name1, name2);
					result_set.insert(subj);
					if (output.size() >= bound) return;
				}

				// check for timeout
				stop = timer::now();
				total_time = duration_cast<seconds>(stop - start).count();
				time_out = (total_time > TIME_OUT);
			}
		}
		else if (s != 0 and o == 0) // const to var
		{
			// find interval of objects to which there is a p-labeled edge from s
			auto I_S = L_P.backward_step(L_P.get_C(s), L_P.get_C(s+1)-1, p_rev);
			uint64_t c = C_S[p_rev];
			I_S.first += c;
			I_S.second += c;

			uint64_t obj = 0;
			for (uint64_t i = I_S.first; !time_out && i <= I_S.second; i++)
			{
				if (!has_matching_attributes(i, 0, attr_acceptors)) continue;

				obj = L_S[i];
				auto iter = result_set.find(obj);
				if (iter == result_set.end()) 
				{
					std::string name1 = SO_id_to_name.at(s);
					std::string name2 = SO_id_to_name.at(obj);
					output.emplace_back(name1, name2);
					result_set.insert(obj);
					if (output.size() >= bound) return;
				}

				stop = timer::now();
				total_time = duration_cast<seconds>(stop - start).count();
				time_out = (total_time > TIME_OUT);
			}
		}
		else if (s == 0 and o == 0) // var to var
		{
			// go through all p-labeled edges to find useful objects
			uint64_t obj = 0;
			for (uint64_t i = C_S[p_rev]; i < C_S[p_rev+1]; i++)
			{
				obj = L_S[i];
				auto iter = result_set.find(obj);
				if (iter == result_set.end())
				{
					// run single predicate query (var to const) to find subjects
					single_predicate_query(p, true, 0, obj, attributes_map, attr_acceptors, output);
					if (output.size() >= bound) return;
					result_set.insert(obj);
				}
				
				stop = timer::now();
				total_time = duration_cast<seconds>(stop - start).count();
				time_out = (total_time > TIME_OUT);
			}
		}
		else if (s != 0 and o != 0) // const to const
		{
			// go through all p-labeled edges to o, and see if any has s as subject
			auto I_S = L_P.backward_step(L_P.get_C(o), L_P.get_C(o+1)-1, p);
			uint64_t c = C_S[p];
			I_S.first += c;
			I_S.second += c;

			for (uint64_t i = I_S.first; !time_out && i <= I_S.second; i++)
			{
				if (!has_matching_attributes(i, 0, attr_acceptors)) continue;

				if (L_S[i] == s)
				{
					std::string name1 = SO_id_to_name.at(s);
					std::string name2 = SO_id_to_name.at(o);
					output.emplace_back(name1, name2);
					return;
				}

				// check for timeout
				stop = timer::now();
				total_time = duration_cast<seconds>(stop - start).count();
				time_out = (total_time > TIME_OUT);
			}
		}
	}




	// Solves the given RPQ starting from initial_object forwards.
	// Collects result path endpoint pairs in output.
	void rpq_const_s_to_var_o(const std::string &rpq,
	                          unordered_map<std::string, uint64_t> &predicates_map,
	                          unordered_map<std::string, AttrInfo> &attributes_map,
	                          uint64_t initial_object,
	                          std::vector<std::pair<std::string, std::string>> &output,
	                          uint64_t bound = std::numeric_limits<uint64_t>::max())
	{

		// create an automaton from the query
		int64_t parse_i = rpq.size() - 1;
		std::string query = parse_reverse(rpq, parse_i, predicates_map, real_max_P);
		RpqAutomataAttr A(query, predicates_map, attributes_map);
		
		// mark which automaton states are useful for which ranges of predicates 
		mark(A);

		// run the query
		timer::time_point start = timer::now();
		_rpq_const_s_to_var_o(A, initial_object, output, true, start, bound);

		// clean markings
		unmark(A);
	};


	// Solves the given RPQ starting from initial_object backwards.
	// Collects result path endpoint pairs in output.
	void rpq_var_s_to_const_o(const std::string &rpq,
	                          unordered_map<std::string, uint64_t> &predicates_map,
	                          unordered_map<std::string, AttrInfo> &attributes_map,
	                          uint64_t initial_object,
	                          std::vector<std::pair<std::string, std::string>> &output,
	                          uint64_t bound = std::numeric_limits<uint64_t>::max())
	{
		// create an automaton from the query
		int64_t parse_i = 0;
		std::string query = parse(rpq, parse_i, predicates_map, real_max_P);
		RpqAutomataAttr A(query, predicates_map, attributes_map);

		// run the query
		mark(A);
		timer::time_point start = timer::now();
		_rpq_const_s_to_var_o(A, initial_object, output, false, start, bound);
		unmark(A);
	};


	// Solves queries where both endpoints are given.
	// Returns in output vector a single pair (s,o) 
	// iff a path matching the RPQ exists between s and o.
	void rpq_const_to_const(const std::string &rpq, uint64_t s, uint64_t o,
	                        unordered_map<std::string, uint64_t> &predicates_map,
	                        unordered_map<std::string, AttrInfo> &attributes_map,
	                        std::vector<std::pair<std::string, std::string>> &output)
	{

		// create an automaton from the query
		int64_t parse_i = 0;
		std::string query = parse(rpq, parse_i, predicates_map, real_max_P);
		RpqAutomataAttr A(query, predicates_map, attributes_map);
		
		// mark which automaton states are useful for which ranges of predicates 
		mark(A);

		// run the query
		high_resolution_clock::time_point start = high_resolution_clock::now();
		if (_rpq_const_to_const(A, s, o, start))
		{
			std::string name1 = SO_id_to_name.at(s);
			std::string name2 = SO_id_to_name.at(o);
			output.emplace_back(name1, name2);
		}

		// clean markings
		unmark(A);
	};




	// Solves the given RPQ, collecting result path endpoint pairs in output.
	// First finds all nodes from which there is some path satisfying the query,
	// then searches in the opposite direction to find the actual pairs.
	void rpq_var_to_var_os(const std::string &rpq,
	                       unordered_map<std::string, uint64_t> &predicates_map,
	                       unordered_map<std::string, AttrInfo> &attributes_map,
	                       std::vector<std::pair<std::string, std::string>> &output,
	                       uint64_t bound = std::numeric_limits<uint64_t>::max())
	{
		// create an automaton from the query
		int64_t parse_i = 0;
		std::string query = parse(rpq, parse_i, predicates_map, real_max_P);
		RpqAutomataAttr A(query, predicates_map, attributes_map);

		// find nodes from which there is a path
		std::vector<uint64_t> subjects;
		mark(A);
		timer::time_point start = timer::now();
		bool all = rpq_var_to_var_obtain_o(A, subjects, start, bound);
		unmark(A);

		// then in the opposite direction
		
		parse_i = rpq.size() - 1;
		std::string query2 = parse_reverse(rpq, parse_i, predicates_map, real_max_P);
		RpqAutomataAttr A2(query2, predicates_map, attributes_map);

		mark(A2);
		bool time_out = false;
		if (all) // there are results to the query from all nodes
		{
			uint64_t sigma = (max_O > max_S) ? max_O : max_S; // largest node id
			for (uint64_t i = 1; !time_out && i <= sigma && output.size() < bound; i++)
				time_out = _rpq_const_s_to_var_o(A2, i, output, true, start, bound);
		}
		else // there are results only from the obtained subjects
		{	   
			for (uint64_t i = 0; !time_out && i < subjects.size() && output.size() < bound; i++)
				time_out = _rpq_const_s_to_var_o(A2, subjects[i], output, true, start, bound);
		}
		unmark(A2);
	};

	// Solves the given RPQ, collecting result path endpoint pairs in output.
	// First finds all nodes to which there is some path satisfying the query,
	// then searches in the opposite direction to find the actual pairs.
	void rpq_var_to_var_so(const std::string &rpq,
	                       unordered_map<std::string, uint64_t> &predicates_map,
	                       unordered_map<std::string, AttrInfo> &attributes_map,
	                       std::vector<std::pair<std::string, std::string>> &output,
	                       uint64_t bound = std::numeric_limits<uint64_t>::max())
	{
		// create an automaton from the query
		int64_t parse_i = rpq.size() - 1;
		std::string query2 = parse_reverse(rpq, parse_i, predicates_map, real_max_P);
		RpqAutomataAttr A2(query2, predicates_map, attributes_map);

		// mark which automaton states are useful for which ranges of predicates 
		mark(A2);
		// find nodes to which there is a path
		std::vector<uint64_t> objects;
		timer::time_point start = timer::now();
		bool all = rpq_var_to_var_obtain_o(A2, objects, start, bound);
		// clean markings
		unmark(A2);

		// then in the opposite direction

		parse_i = 0;
		std::string query = parse(rpq, parse_i, predicates_map, real_max_P);
		RpqAutomataAttr A(query, predicates_map, attributes_map); 

		mark(A);
		bool time_out = false;
		if (all) // there are results to the query from all nodes
		{
			uint64_t sigma = (max_O > max_S) ? max_O : max_S; // largest node id
			for (uint64_t i = 1; !time_out && i <= sigma && output.size() < bound; i++)
				time_out = _rpq_const_s_to_var_o(A, i, output, false, start, bound);
		}
		else // there are results only from the obtained objects
		{
			for (uint64_t i = 0; !time_out && i < objects.size() && output.size() < bound; i++)
				time_out = _rpq_const_s_to_var_o(A, objects[i], output, false, start, bound);
		}
		unmark(A);
	};





public:

	// Evaluates the given regular path query.
	// Collects results in output as pairs of path endpoint names.
	// Parameters:
	//   query_type: the sequence of operators in the query
	//   pred_vec: the sequence of predicates in the query
	//   s_id: the starting node's identifier, or 0 if none
	//   o_id: the ending node's identigier, or 0 if none
	//   pred_map: mapping from predicate names to ids
	//   attr_map: mapping from attribute names to type information structs
	//   output: vector for the returned results
	// (Copied with small modifications from query-index-basic.cpp)
	void run_query(std::string query, std::string query_type, 
	               std::vector<uint64_t>& pred_vec, 
	               uint64_t s_id, uint64_t o_id,
	               std::unordered_map<std::string, uint64_t>& pred_map,
	               std::unordered_map<std::string, AttrInfo>& attr_map,
	               std::vector<std::pair<std::string, std::string>>& output,
	               uint64_t bound = std::numeric_limits<uint64_t>::max())
	{
		// Choose correct method to use for evaluation:

		if (query_type == "" and pred_vec.back() != 0) // single predicate
		{
			vector<vector<pair<AttrInfo, Acceptor>>> attr_acceptors;
			query = parse_attribute_conditions(query, attr_acceptors, attr_map);
			
			// check if the predicate name contains %, i.e. is reversed
			bool is_reversed_pred = false;
			for (unsigned i = 0; i < query.length(); i++) 
				if (query.at(i) == '%') { is_reversed_pred = true; break; }
			
			single_predicate_query(pred_vec.back(), is_reversed_pred, s_id, o_id, 
			                       attr_map, attr_acceptors, output, bound);
		}
		else if (s_id == 0 and o_id == 0) // both endpoints are variable
		{
			// determine the direction to search (subjects first or objects first)
			if (query_type == "") // single wildcard predicate
				rpq_var_to_var_so(query, pred_map, attr_map, output, bound); 
			else if (query_type == "+" or query_type == "*")
				rpq_var_to_var_so(query, pred_map, attr_map, output, bound);
			else if (query_type[query_type.size() - 1] == '*' and query_type[0] != '*')
				rpq_var_to_var_so(query, pred_map, attr_map, output, bound);
			else if (query_type[0] == '*' or query_type[0] == '+')
				rpq_var_to_var_os(query, pred_map, attr_map, output, bound);
			else if (std::all_of(query_type.begin(), query_type.end(), [](char c){return c == '|';})
				 and std::all_of(pred_vec.begin(), pred_vec.end(), [](uint64_t id){return id != 0;}))
				or_query_var_to_var(query, pred_vec.size(), pred_map, attr_map, output, bound);
			else if (pred_selectivity(pred_vec.front()) <= pred_selectivity(pred_vec.back()))
				rpq_var_to_var_so(query, pred_map, attr_map, output, bound);
			else
				rpq_var_to_var_os(query, pred_map, attr_map, output, bound);
		} 
		else // at least one endpoint is fixed
		{
			if (s_id != 0 and o_id == 0)
				rpq_const_s_to_var_o(query, pred_map, attr_map, s_id, output, bound);
			else if (s_id == 0 and o_id != 0)
				rpq_var_s_to_const_o(query, pred_map, attr_map, o_id, output, bound);
			else
				rpq_const_to_const(query, s_id, o_id, pred_map, attr_map, output);
		}
	}


	// Adds auxiliary attributes to which results from subqueries will be stored.
	void prepare_nested_query(std::unordered_map<std::string, AttrInfo>& attributes_map, 
	                          unsigned int n_of_subqueries)
	{
		for (unsigned int i = 0; i < n_of_subqueries; i++)
		{
			int_attributes.emplace_back();
			attributes_map["_" + std::to_string(i) + "_"] 
					= AttrInfo{int_attributes.size()-1, AttrInfo::Node, AttrInfo::Int};
		}
	}
	// Removes the added auxiliary attributes.
	void cleanup_nested_query(unsigned int n_of_subqueries)
	{
		for (unsigned int i = 0; i < n_of_subqueries; i++)
		{
			int_attributes.pop_back();
		}
	}

	// Evaluates the q_num-th branched subquery, 
	// finding the nodes from which there exist a path matching the given rpq.
	// Results are saved to auxiliary attributes.
	// Returns true if the query accepts even empty paths.
	bool run_subquery(unsigned int q_num, std::string rpq, 
					std::unordered_map<std::string, uint64_t>& predicates_map,
					std::unordered_map<std::string, AttrInfo>& attributes_map)
	{
		// find id of the auxiliary attribute reserved for this subquery
		int q_attr = attributes_map.at("_" + std::to_string(q_num) + "_").id;

		// prepend the query with a reflexive edge,
		// which will be used for setting the auxiliary attributes
		rpq = NODE_PREDICATE + "(" + rpq + ")";

		// create an automaton from the query
		int64_t parse_i = 0;
		rpq = parse(rpq, parse_i, predicates_map, real_max_P);
		RpqAutomataAttr A(rpq, predicates_map, attributes_map);

		// mark which automaton states are useful for which ranges of predicates 
		mark(A);

		// find startpoints of paths that match the rpq,
		// setting their value of q_attr to 1
		// (unless all nodes would trivially match)
		timer::time_point start = timer::now();
		bool is_trivial = rpq_subquery_obtain_o(q_attr, A, start);

		// clean markings
		unmark(A);

		return is_trivial;
	}



	// Implemented by Terho Thorel.
	// Collects all paths whose length is the given depth,
	// starting from the node with the given name.
	// Each returned path contains the sequence of predicate names
	// with the endpoint node name appended to its end.
	// Edges are followed either forwards, backwards, or both,
	// depending on the boolean arguments.
	void find_paths(std::string start_node, unsigned int depth, 
	                std::vector<std::vector<std::string>>& paths,
	                bool forward = true, bool backward = false, 
	                timer::time_point start_time = timer::now())
	{
		if (SO_name_to_id.find(start_node) == SO_name_to_id.end())
		{
			throw std::invalid_argument("Unknown node: " + start_node);
		}
		uint64_t start_id = SO_name_to_id.at(start_node);

		// recursive depth-first-search algorithm
		// (paths.back() is the current path)

		// create empty path on first call
		if (paths.size() == 0) paths.push_back({});

		if (paths.back().size() < depth)
		{
			// find the current node's interval in L_P
			uint64_t left = L_P.get_C(start_id);
			uint64_t right = L_P.get_C(start_id + 1) - 1;
			// go through all possible predicates
			std::vector<uint64_t> p_vec;
			L_P.all_values_in_range_bounded(left, right, p_vec, right - left + 1);
			for (uint64_t p : p_vec)
			{
				if (p == 0 or p == real_max_P) continue; // are not actual edges

				if ((forward and p < real_max_P) or (backward and p > real_max_P))
				{
					// find the interval in L_S for nodes reached with p
					std::pair<uint64_t, uint64_t> I_s = L_P.backward_step(left, right, p);
					I_s.first += C_S[p];
					I_s.second += C_S[p];
					for (uint64_t i = I_s.first; i <= I_s.second; i++)
					{
						// advance recursively
						paths.back().push_back(P_id_to_name.at(p % real_max_P));
						std::string next_node = SO_id_to_name.at(L_S[i]);
						find_paths(next_node, depth, paths, forward, backward, start_time);
					}
				}

				auto time = timer::now();
				auto time_span = duration_cast<seconds>(time - start_time);
				if (time_span.count() > TIME_OUT) break;
			}
		}
		else
		{
			// save the resulting path with the endpoint node name
			paths.back().push_back(SO_id_to_name.at(start_id));
			// continue with the same path (without end node)
			paths.emplace_back(paths.back());
			paths.back().pop_back();
		}

		// retract along the path
		if (paths.back().size() > 0)
		{
			paths.back().pop_back();
		}
		else paths.pop_back(); // remove empty path when recursion ends
	}

};


typedef ring_rpq_attr<std::stack<interval_state_type>> ring_rpq_attr_dfs;
typedef ring_rpq_attr<std::queue<interval_state_type>> ring_rpq_attr_bfs;

#endif

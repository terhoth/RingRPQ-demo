/*
 * File: parse_query_attr.cpp
 * Author: Terho Thorel, except for parse and parse_reverse,
 *  which have just been modified to be compatible with attributes.
 * Description: functions related to parsing regular path queries
 */

/*
 * triple_bwt_rpq.hpp
 * Copyright (C) 2020 Author name removed for double blind evaluation
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

#ifndef PARSE_QUERY
#define PARSE_QUERY

#include <cstdint>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <functional>

using std::vector;
using std::unordered_map;
using std::string;
using std::function;
using std::pair;

enum class ParseError 
{ 
	None, 
	MismatchedAngle, 
	MismatchedSquare, 
	MisplacedSquare, 
	UnknownPredicate, 
	UnknownAttribute,
	InvalidCondition,
	InvalidCurlyBrackets, 
	MissingEndpoint
};




// name of the auxiliary attribute used to store node labels
const std::string NODELABEL_ATTR = "__NODELABEL__";
// prefix for node labels
const char NODE_SYMBOL = '&';
// shorthand for <&>; matches any node
const std::string NODE_PREDICATE = {'<', NODE_SYMBOL, '>'};
const std::string WILDCARD_PREDICATE = "<_>";

// Just toggles predicate reversion, keeping the expression's structure unchanged.
std::string parse(const std::string& rpq, int64_t& i,
	          std::unordered_map<std::string, uint64_t>& predicates_map, uint64_t max_P)
{
	std::string str_aux;

	while ((unsigned)i < rpq.size()) {
		if (rpq[i]==')') // closing bracket means this (sub)expression ends
			return str_aux;
		else if (rpq[i] == '<') { // a predicate starts
			std::string s("<");
			i++;
			// read the predicate name to str_aux
			if (rpq[i]=='%') { // is the predicate reversed?
				i++; // remove %
				bool reading_attrs = false;
				while ((unsigned)i < rpq.size() and (rpq[i]!='>' or reading_attrs)) 
				{
					s += rpq[i];
					if (rpq[i] == '[') { reading_attrs = true; }
					if (rpq[i] == ']') { reading_attrs = false; }
					i++;
				}
				str_aux += s + '>';
				i++;
			} else { // not reversed, add %
				std::string s_aux("<");
				s += '%';
				bool reading_attrs = false;
				while ((unsigned)i < rpq.size() and (rpq[i]!='>' or reading_attrs)) 
				{
					s += rpq[i];
					if (rpq[i] == '[') { reading_attrs = true; }
					if (!reading_attrs) { s_aux += rpq[i]; }
					if (rpq[i] == ']') { reading_attrs = false; }
					i++;
				}
				s += '>';
				s_aux += '>';
				if (s_aux == NODE_PREDICATE || s_aux == WILDCARD_PREDICATE) // reflexive edge stays same backwards
					predicates_map["<%" + s_aux.substr(1)] = predicates_map[s_aux];
				else // record shifted predicate id
					predicates_map["<%" + s_aux.substr(1)] = max_P + predicates_map[s_aux];
				str_aux += s;
				i++;
			}
		} else if (rpq[i]=='(') { // subexpression starts, recurse
			i++;
			std::string s = parse(rpq, i, predicates_map, max_P);
			str_aux += '(' + s + ')';
			i++;
		} else { // operator
			str_aux += rpq[i];
			i++;
		}
	}
	return str_aux;
};

// Reverses the expression's structure, keeping predicates unchanged.
std::string parse_reverse(const std::string& rpq, int64_t& i, 
                          std::unordered_map<std::string, uint64_t>& predicates_map, uint64_t max_P)
{
	std::string str_aux;
	char _operator;

	while (i >= 0) {
		if (rpq[i]=='(') // this (sub)expression ends
			return str_aux;
		else if (rpq[i]=='*' or rpq[i]=='+' || rpq[i]=='?') { // postfix operator
			_operator = rpq[i];

			if (rpq[i-1]==')') { // recurse on the operand
				i -= 2;
				std::string s = parse_reverse(rpq, i, predicates_map, max_P);
				//std::reverse(s.begin(), s.end());
				str_aux += '(' + s + ')' + _operator;
				i--;
			} else { // single predicate as operand
				std::string s(">");
				std::string s_aux(">");
				i--;
				bool reading_attrs = false;
				while (i >= 0 and (rpq[i]!='<' or reading_attrs )) // read the predicate name backwards...
				{
					s += rpq[i];
					if (rpq[i] == ']') { reading_attrs = true; }
					if (!reading_attrs and rpq[i] != '%') { s_aux += rpq[i]; }
					if (rpq[i] == '[') { reading_attrs = false; }
					i--;
				}
				s += '<';
				s_aux += '<';
				std::reverse(s.begin(), s.end()); // ...and turn it to the correct direction
				if (s[1] == '%') {
					std::reverse(s_aux.begin(), s_aux.end());
					if (s_aux == NODE_PREDICATE || s_aux == WILDCARD_PREDICATE) // reflexive edge stays same backwards
						predicates_map["<%" + s_aux.substr(1)] = max_P;
					else // record shifted id if reversed
						predicates_map["<%" + s_aux.substr(1)] = max_P + predicates_map[s_aux];
				}
				str_aux += s + _operator;
				i--;
			}
		} else if (rpq[i] == '>') { // predicate starts
			std::string s(">");
			std::string s_aux(">"); 
			i--;
			bool reading_attrs = false;
			while (i >= 0 and (rpq[i]!='<' or reading_attrs)) // read the predicate name backwards...
			{
				s += rpq[i];
				if (rpq[i] == ']') { reading_attrs = true; }
				if (!reading_attrs and rpq[i] != '%') { s_aux += rpq[i]; }
				if (rpq[i] == '[') { reading_attrs = false; }
				i--;
			}
			s += '<';
			s_aux += '<';
			std::reverse(s.begin(), s.end()); // ...and turn it to the correct direction
			if (s[1] == '%') {
				std::reverse(s_aux.begin(), s_aux.end());
				if (s_aux == NODE_PREDICATE || s_aux == WILDCARD_PREDICATE) // reflexive edge stays same backwards
					predicates_map["<%" + s_aux.substr(1)] = max_P;
				else // record shifted predicate if reversed
					predicates_map["<%" + s_aux.substr(1)] = max_P + predicates_map[s_aux];
			}
			str_aux += s;
			i--;
		} else if (rpq[i]==')') { // subexpression starts, recurse
			--i;
			std::string s = parse_reverse(rpq, i, predicates_map, max_P);
			str_aux += '(' + s + ')';
			i--;
		} else { // operator
			str_aux += rpq[i];
			i--;
		}
	}
	return str_aux;
};



// Parses an attribute condition extracted from between a pair of square brackets.
// Expects the syntax 'attribute relation value'. 
// Only one condition per pair of brackets!
// For example '<orders[quantity >= 10][cost <= 25.99]>' could be part of a RPQ.
bool parse_condition(const string& attr_string,
                     vector<vector<pair<AttrInfo, Acceptor>>>& acceptors,
                     const unordered_map<string, AttrInfo>& attributes_map)
{
	//printf("parse_condition(%s)\n", attr_string.c_str());

	stringstream attr_stream(attr_string);

	string attr_name;
	string relation;
	string value_str;

	// read attribute name, comparison relation, and value to compare to
	attr_stream >> attr_name;
	attr_stream >> relation;
	attr_stream >> value_str;

	// get attribute id
	if (attributes_map.find(attr_name) == attributes_map.end()) return false;
	AttrInfo attr = attributes_map.at(attr_name);

	// create acceptor (see ../nfa/include/attr_info.hpp)
	acceptors.back().emplace_back(attr, Acceptor(attr, relation, value_str));

	return true;
}


// Parses the rpq, extracting constraints for attributes.
// Returns the rest of the string after removing square bracketed text.
// The conditions are stored in acceptors as tuples of:
// 0. the position in the query
// 1. struct containing the attribute's identifier and type
// 2. a function object that checks the condition for any given value
string parse_attribute_conditions(const string& rpq,
                                  vector<vector<pair<AttrInfo, Acceptor>>>& acceptors,
                                  const unordered_map<string, AttrInfo>& attributes_map)
{
	string rpq_without_attrs = "";
	bool reading_attrs = false;
	string attr_str = "";
	unsigned int i = 0;
	while (i < rpq.length())
	{
		if (rpq[i] == ']')
		{
			reading_attrs = false;
			parse_condition(attr_str, acceptors, attributes_map);
		}
		
		if (reading_attrs)
		{
			attr_str += rpq[i];
		}
		else if (rpq[i] != '[' and rpq[i] != ']')
		{
			rpq_without_attrs += rpq[i];
			if (rpq[i] == '<')
			{
				// add empty vector for the next predicate's conditions
				acceptors.emplace_back();
			}
		}

		if (rpq[i] == '[')
		{
			reading_attrs = true;
			attr_str.clear();
		}

		i++;
	}

	return rpq_without_attrs;
}

// Returns the rest of the string after removing square bracketed text.
string remove_attribute_conditions(const string& rpq)
{
	string rpq_without_attrs = "";
	bool reading_attrs = false;
	unsigned int i = 0;
	while (i < rpq.length())
	{
		if (rpq[i] == '[') reading_attrs = true;
		
		if (!reading_attrs)
		{
			rpq_without_attrs += rpq[i];
		}

		if (rpq[i] == ']') reading_attrs = false;

		i++;
	}

	return rpq_without_attrs;
}


// Extracts {}-enclosed subqueries from the query,
// returning the main query with subqueries replaced by auxiliary attributes.
// Returns an empty string if there is a syntax error.
// This is the first step of parsing, so other functions assume no curly brackets are present.
string extract_nested_subqueries(const string& rpq, vector<string>& subqueries, 
                                 unordered_map<string, unsigned int>* subquery_map)
{
	string query = "";
	string subquery = "";
	unsigned int depth = 0; // is 0 iff currently not inside curly brackets
	for (unsigned int i = 0; i < rpq.length(); i++)
	{
		if (rpq[i] == '}') 
		{
			if (depth == 0) return ""; // closing bracket without opening bracket
			depth--;
			if (depth == 0) // subquery ends
			{
				// recurse on the subquery
				subquery = extract_nested_subqueries(subquery, subqueries, subquery_map);
				// check whether an identical subquery was already found,
				// in which case there is no need to re-evaluate it
				if (subquery_map->find(subquery) == subquery_map->end())
				{
					subqueries.push_back(subquery);
					(*subquery_map)[subquery] = subquery_map->size();
				}
				// add auxiliary attribute condition
				string aux_attr_name = "_" + std::to_string(subquery_map->at(subquery)) + "_";
				query += "[" + aux_attr_name + " = 1]";
			}
		}

		// append current char to either the main query or the subquery
		if (depth > 0) subquery += rpq[i];
		else if (rpq[i] != '{' and rpq[i] != '}') query += rpq[i];

		if (rpq[i] == '{') 
		{
			if (i == 0 or rpq[i-1] != NODE_SYMBOL) return ""; // branching allowed only at nodes
			if (depth == 0) subquery.clear(); // subquery begins
			depth++;
		}
	}
	if (depth > 0) return ""; // opening bracket without closing bracket

	return query;
}

string extract_nested_subqueries(const string& rpq, vector<string>& subqueries)
{
	// initialize recursion variable
	// for identifying and optimizing identical subqueries
	unordered_map<string, unsigned int> subquery_map;
	// call the actual implementation (above)
	return extract_nested_subqueries(rpq, subqueries, &subquery_map);
}


// Replace node labels <&Node> with attributes <&[__NODELABEL__  = Node]>
string parse_node_labels(const string& rpq)
{
	string new_rpq = "";
	bool reading_node_label = false;
	string node_label = "";
	for (unsigned int i = 0; i < rpq.length(); i++)
	{
		if (reading_node_label)
		{
			// The node label can end to either [ or >: 
			// <&Node> or <&Node[attr = value]>
			if (rpq[i] == '[' or rpq[i] == '>')
			{
				reading_node_label = false;
				if (node_label != "")
				{
					// add auxiliary attribute condition
					new_rpq += "[" + NODELABEL_ATTR + " = " + "<"+node_label+">" + "]" + rpq[i];
				}
				else new_rpq += rpq[i];
			}
			else node_label += rpq[i];
		}
		else new_rpq += rpq[i];

		// check whether a node label begins here: <&... or <%&...
		if (i > 0 and rpq[i-1] == '<' and rpq[i] == NODE_SYMBOL)
		{
			reading_node_label = true;
		}
		else if (i > 1 and rpq[i-2] == '<' and rpq[i-1] == '%' and rpq[i] == NODE_SYMBOL)
		{
			// remove reversion symbol % as it's meaningless on nodes
			new_rpq = new_rpq.substr(0, new_rpq.length()-2) + NODE_SYMBOL;
			reading_node_label = true;	
		}
	}
	return new_rpq;
}

// Parses the input_rpq to parsed_rpq,
// omitting concatenation operators, changing ^ to %, and removing whitespace.
// Records the sequence of operators to query_type,
// and the sequence of predicates to pred_vec.
// Checks existence of attribute and predicate names 
// and collects them to pred_map and attr_map.
// Returns true if the query syntax is valid.
ParseError parse_query(const string& input_rpq, string& parsed_rpq, 
                       string& query_type, vector<uint64_t>& pred_vec,
                       const unordered_map<string, uint64_t>& map_P, // all predicates
                       unordered_map<string, uint64_t>& pred_map, // only those needed
                       const unordered_map<string, AttrInfo>& map_A, // all attributes
                       unordered_map<string, AttrInfo>& attr_map) // only those needed
{
	const string rpq = parse_node_labels(input_rpq);

	parsed_rpq.clear();
	query_type.clear();
	pred_vec.clear();

	bool reading_preds = false;
	bool reading_attrs = false;
	bool reading_attr_name = false;
	string pred_name = "";
	string attr_name = "";
 
	for (unsigned int i = 0; i < rpq.length(); i++)
	{
		if (rpq[i] == '<' and not reading_attrs) // predicate begins
		{
			if (reading_preds) return ParseError::MismatchedAngle;
			reading_preds = true; 
			pred_name.clear();
		}
		else if (rpq[i] == '>' and not reading_attrs) // predicate ends
		{
			if (not reading_preds) return ParseError::MismatchedAngle;
			reading_preds = false;
			pred_name += '>';
			if (map_P.find(pred_name) == map_P.end()) return ParseError::UnknownPredicate;
			pred_map[pred_name] = map_P.at(pred_name);
			pred_vec.push_back(map_P.at(pred_name));
		}
		else if (rpq[i] == '[') // attribute begins
		{
			if (not reading_preds) return ParseError::MisplacedSquare;
			if (reading_attrs) return ParseError::MismatchedSquare;
			reading_attrs = true;
			reading_attr_name = true;
			attr_name.clear();
		}
		else if (rpq[i] == ']') // attribute ends
		{
			if (not reading_attrs) return ParseError::MismatchedSquare;
			if (reading_attr_name) return ParseError::InvalidCondition;
			reading_attrs = false;
		}
		else if (rpq[i] == ' ' and reading_attr_name) // attribute name ends
		{
			if (attr_map.find(attr_name) == attr_map.end())
			{
				if (map_A.find(attr_name) == map_A.end()) return ParseError::UnknownAttribute;
				attr_map[attr_name] = map_A.at(attr_name);   
			}
			reading_attr_name = false;
		}

		// append the current char to appropriate strings
		if (reading_attrs)
		{
			if (reading_attr_name and rpq[i] != '[') attr_name += rpq[i];
			parsed_rpq += rpq[i];
		}
		else if (reading_preds)
		{
			if (rpq[i] != '%' and rpq[i] != ']') pred_name += rpq[i];
			parsed_rpq += rpq[i];
		}
		else 
		{
			if (rpq[i] == '^') parsed_rpq += '%';
			else if (rpq[i] != '/' and rpq[i] != ' ') parsed_rpq += rpq[i];

			if (rpq[i] != '(' and rpq[i] != ')' and rpq[i] != ' ' and rpq[i] != '>') query_type += rpq[i];
		} 
	}

	return ParseError::None;
}


// Finds whether subject/object are constants or variables.
// If constants, returns their ids, otherwise returned id is zero.
// Returns the actual rpq part of the query, 
// or an empty string in case of parse error.
std::tuple<uint64_t, string, uint64_t> parse_subject_and_object(const string& query, const unordered_map<string, uint64_t>& map_SO)
{
	// read subject string
	string subject = "";
	unsigned int i = 0;
	while (i < query.length() and std::isspace(query[i])) i++;
	while (i < query.length() and not std::isspace(query[i])) 
	{
		subject += query[i];
		i++;
	}
	while (i < query.length() and std::isspace(query[i])) i++;
	
	// check validity
	if (i == query.length() - 1)
	{
		return std::tuple<uint64_t, string, uint64_t>(0, "", 0);
	}

	// read object string
	string object = "";
	unsigned int j = query.length() - 1;
	while (std::isspace(query[j])) j--;
	while (not std::isspace(query[j]))
	{
		object += query[j];
		j--;
	}
	while (std::isspace(query[j])) j--;
	std::reverse(object.begin(), object.end());

	// find subject id
	uint64_t subject_id = 0;
	if (subject.at(0) != '?')
	{
		auto iter = map_SO.find(subject);
		if (iter == map_SO.end())
		{
			return std::tuple<uint64_t, string, uint64_t>(0, "", 0);
		}
		subject_id = iter->second;
	}

	// find object id
	uint64_t object_id = 0;
	if (object.at(0) != '?')
	{
		auto iter = map_SO.find(object);
		if (iter == map_SO.end())
		{
			return std::tuple<uint64_t, string, uint64_t>(0, "", 0);
		}
		object_id = iter->second;
	}

	// extract the rest of the query
	std::string rpq = query.substr(i, j-i+1);

	return std::tuple<uint64_t, string, uint64_t>(subject_id, rpq, object_id);
}

#endif

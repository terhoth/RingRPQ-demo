/*
 * File: RpqAutomata_attr.cpp
 * Author: modified by Terho Thorel, from RpqAutomata.cpp by Adrian Brandon
 * Description: a class to adapt Glushkov automata to query attributed RPQs using the ring index
 *
 */

#include <iostream>
#include <string>
#include <vector>
#include <regex>
#include <sstream>
#include <functional>
#include "../include/parser.hpp"
#include "../include/regular.hpp"
#include "../include/RpqAutomata_attr.hpp"
#include "../include/attr_info.hpp"

using std::pair;
using std::string;
using std::unordered_map;
using std::function;
using std::get;
using std::vector;

uint64_t RpqAutomataAttr::fwdTrans(Mask *trans, int current, Mask predB)
{
    return *trans[current] & (*predB | *_B_any);
}

uint64_t RpqAutomataAttr::bwdTrans(Mask *trans, int current, Mask predB)
{
    return *trans[current & (*predB | *_B_any)];
}

uint64_t RpqAutomataAttr::next_1SliceInT(const regularData *nfaData, uint64_t current, uint64_t predicate, int type, Mask b)
{
    if (type == FWD)
        return fwdTrans(nfaData->fwdTrans[0], current, b);
    else
        return bwdTrans(nfaData->bwdTrans[0], current, b);
}

uint64_t RpqAutomataAttr::next(uint64_t current, uint64_t predicate, int type)
{
    Mask bPos = createMask(64);
    COPY(bPos, _B_any, 64);
    if (predicate != 0)
    {
        const auto mapItem = _B.find(predicate);
        if (mapItem != _B.end())
            OR(bPos, mapItem->second, 64);
    }
    if (ISZERO(bPos, 64)) return 0;


    if (_nfaData->slices == 1)
        return next_1SliceInT(_nfaData, current, predicate, type, bPos);

    // ELSE handle multiple slices
    uint64_t next = 0;
    if (type == FWD)
        for (auto i = 0; i < _nfaData->slices; i++)
            next |= fwdTrans(_nfaData->fwdTrans[0], current, bPos);
    else
        for (auto i = 0; i < _nfaData->slices; i++)
            next |= bwdTrans(_nfaData->bwdTrans[0], current, bPos);

    return next;
}

regularData *buildNFA(const char *pattern, int length)
{
    Tree *tree;
    Tree **pos;
    int m;

    m = length;
    pos = new Tree *[m];
    tree = parse(pattern, m, pos);

    if ((tree == NULL))
    {
        freeTree(tree);
        free(pos);
        return NULL;
    }

    return regularPreproc(pattern, tree, pos);
}

// Modified by Terho Thorel to take into account bracketed attribute constraints
pair<string, unordered_map<string, char>> rpqToStringPattern(const string &rpq, const unordered_map<string, uint64_t> &predicates)
{
    unordered_map<string, char> map;
    char firstUnassigned = 1;
    auto pattern = rpq;

    std::regex predRE("(<[^>\\[\\]]*(?:\\[[^\\[\\]]*\\])*>)");
    std::sregex_iterator next(rpq.begin(), rpq.end(), predRE);
    std::sregex_iterator end;

    while (next != end)
    {
        std::smatch match = *next;
        auto str = match.str();
        if (map.find(str) == map.end())
        {
            map[str] = firstUnassigned;

            str = std::regex_replace(str, std::regex("(\\[)"), "\\[");
            str = std::regex_replace(str, std::regex("(\\])"), "\\]");

            std::regex replaceRE(str);
            pattern = std::regex_replace(pattern, replaceRE, string(1, firstUnassigned));

            ++firstUnassigned;
        }
        next++;
    }

    std::regex slashRE("/");
    pattern = std::regex_replace(pattern, slashRE, "");

    return {pattern, map};
}

pair<regularData *, unordered_map<string, char>> build(const string &rpq, const unordered_map<string, uint64_t> &predicates)
{
    // 1. Transform RPQ to a regular expression
    auto patternAndMap = rpqToStringPattern(rpq, predicates);
    auto pattern = patternAndMap.first;
    // 2. Use nrgrep to parse the pattern and build the NFA
    auto regularData = buildNFA(pattern.c_str(), pattern.length());
    return {regularData, patternAndMap.second};
}
/*
pair<regularData *, unordered_map<uint64_t, char>> build(const string &pattern, const unordered_map<uint64_t , char> &map)
{


    // 2. Use nrgrep to parse the pattern and build the NFA
    auto regularData = buildNFA(pattern.c_str(), pattern.length());

    return {regularData, map};
}
*/
RpqAutomataAttr::RpqAutomataAttr() {
    _nfaData = NULL;
    _isValid = false;
};

// Modified by Terho Thorel
RpqAutomataAttr::RpqAutomataAttr(const string &rpq, 
                                 const unordered_map<string, uint64_t> &predicates,
                                 const unordered_map<string, AttrInfo> &attributes)
{
    auto nfaAndMap = build(rpq, predicates);

    _nfaData = nfaAndMap.first;
    _isValid = _nfaData != NULL;

    _B_any = createMask(64);
    ZERO(_B_any, 64);

    for (auto pair : nfaAndMap.second)
    {
        // parse the predicate name and attribute constraints
        string predStrWithAttrs = pair.first;
        auto states = _nfaData->B[pair.second];
        string predStr = parse_attribute_conditions(predStrWithAttrs, *states, predicates, attributes);
        if (predicates.find(predStr) == predicates.end()) 
        {
            throw std::invalid_argument("unknown predicate: " + predStr); 
        }
        uint64_t predId = predicates.at(predStr);
        //printf("%s %s %lu   %lu\n", predStrWithAttrs.c_str(), predStr.c_str(), predId, *states);

        // update the states that are targeted by edges whose label is predId
        if (predId == 0) // a nameless predicate that matches any edge
        {
            OR(_B_any, states, 64);
        }
        
        if (_B.find(predId) == _B.end())
        {
            _B[predId] = states;
        }
        else
        {
            OR(_B[predId], states, 64);
        }
    }
}

/*
RpqAutomataAttr::RpqAutomataAttr(const string &pattern, const unordered_map<uint64_t , char> &map)
{
    auto nfaAndMap = build(pattern, map);

    _nfaData = nfaAndMap.first;
    _isValid = _nfaData != NULL;

    for (auto pair : nfaAndMap.second)
    {
        _B[pair.first] = _nfaData->B[pair.second];
    }
}
*/

unordered_map<uint64_t, uint64_t> RpqAutomataAttr::getB()
{
    unordered_map<uint64_t, uint64_t> result;

    for (auto pair : _B)
    {
        result[pair.first] = *pair.second;
    }

    return result;
}

RpqAutomataAttr::~RpqAutomataAttr()
{
    if (_nfaData != NULL)
    {
        regularFree(_nfaData);
        _nfaData = NULL;
    }
}




// Implemented by Terho Thorel
string RpqAutomataAttr::parse_attribute_conditions(const string& p_string, uint64_t automaton_states,
                                                   const unordered_map<string, uint64_t>& predicates_map, 
                                                   const unordered_map<string, AttrInfo>& attributes_map)
{

    // First, separate attribute conditions from the predicate name:

    std::vector<string> attr_conditions;
    string p_without_attrs = "";
 
    string attr_condition = "";
    bool reading_attributes = false;
    for (unsigned int i = 0; i < p_string.length(); i++)
    {
        if (p_string[i] == '[')
        {
            reading_attributes = true;
            attr_condition.clear();
        }
        else if (p_string[i] == ']')
        {
            reading_attributes = false;
            attr_conditions.push_back(attr_condition);
        }
        else if (reading_attributes)
        {
            attr_condition += p_string[i];
        }
        else
        {
            p_without_attrs += p_string[i];
        }
    }
    

    // Then, create a lambda acceptor for each condition:
    
    _AttrAcceptors.push_back({});

    // add trivial condition if none explicitly given
    if (attr_conditions.size() == 0)
    {
        _AttrAcceptors.back().emplace_back(automaton_states, AttrInfo{0, AttrInfo::Edge, AttrInfo::None}, Acceptor());
    }

    for (string attr_cond : attr_conditions)
    {
        std::stringstream attr_stream(attr_cond);

        string attr_name;
        string relation;
        string value_str;
        
        // read attribute name, comparison relation, and value to compare to
        attr_stream >> attr_name;
        attr_stream >> relation;
        attr_stream >> value_str;

        // get attribute id
        if (attributes_map.find(attr_name) == attributes_map.end())
        {
            std::cout << "Warning: Unknown attribute name: " << attr_name << std::endl;
            continue;
        }
        AttrInfo attr = attributes_map.at(attr_name);

        // create acceptor (see attr_info.hpp)
        Acceptor acceptor = Acceptor(attr, relation, value_str);
        _AttrAcceptors.back().emplace_back(automaton_states, attr, acceptor);
    }

    return p_without_attrs;
}


// Implemented by Terho Thorel.
uint64_t RpqAutomataAttr::states_with_matching_attributes(uint64_t i, uint64_t p,
                                        const vector<unordered_map<uint64_t, string>>& attributes_str,
                                        const vector<unordered_map<uint64_t, int>>& attributes_int,
                                        const vector<unordered_map<uint64_t, float>>& attributes_flt)
{
    uint64_t result_states = 0;
    for (const auto& disjunct : _AttrAcceptors)
    {
        // find the states accepted by this disjunct's conditions
        uint64_t valid_states = 0;
        uint64_t invalid_states = 0;
        for (const auto& state_attr_acceptor_tuple : disjunct)
        {
            uint64_t states = get<0>(state_attr_acceptor_tuple);
            AttrInfo attr = get<1>(state_attr_acceptor_tuple);
            const Acceptor& acceptor = get<2>(state_attr_acceptor_tuple);

            bool accepted;
            if (attr.type == AttrInfo::None) // indicates trivial condition
            {
                accepted = true;
            }
            else if (attr.type == AttrInfo::Str) 
            {
                if (attributes_str.at(attr.id).find(i) != attributes_str.at(attr.id).end())
                {
                    const string& value = attributes_str.at(attr.id).at(i);
                    accepted = acceptor(value);
                }
                else accepted = acceptor(); // accepts iff the condition tests for null
            }
            else if (attr.type == AttrInfo::Int)
            {
                if (attributes_int.at(attr.id).find(i) != attributes_int.at(attr.id).end())
                {
                    int value = attributes_int.at(attr.id).at(i);
                    accepted = acceptor(value);
                }
                else accepted = acceptor();
            }
            else if (attr.type == AttrInfo::Flt)
            {
                if (attributes_flt.at(attr.id).find(i) != attributes_flt.at(attr.id).end())
                {
                    float value = attributes_flt.at(attr.id).at(i);
                    accepted = acceptor(value);
                }
                else accepted = acceptor();
            }

            if (accepted)
            {
                valid_states |= states; 
            }
            else
            {
                invalid_states |= states;  
            }
        }
        // aggregate the subresult to the disjunction result
        result_states |= valid_states & ~invalid_states;
    }

    return result_states;
}


/*
 * File: RpqAutomata_attr.hpp
 * Author: modified by Terho Thorel, from RpqAutomata.hpp by Adrian Brandon
 * Description: a class to adapt Glushkov automata to query attributed RPQs using the ring index
 *
 */

#ifndef RPQ_AUTOMATA_ATTR_INCLUDED
#define RPQ_AUTOMATA_ATTR_INCLUDED

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <variant>
#include "regular.hpp"
#include "attr_info.hpp"

using std::unordered_map;
using std::vector;

/* A class abstracting an NFA for an RPQ, with edge attributes.
   
   Remarks:
        - Assumes that there are at most 64 different states in the resulting automata
        - Assumes that the predicate names have already been maped to integers, and it 
          requires the map as an argument in the constructor.
        - Predicate names in the RPQ must be enclosed in < > (e.g. <P2119>?/<P2515>).
*/
class RpqAutomataAttr
{

public:

    RpqAutomataAttr();
  /* Creates an instance of an RpqAutomataAttr given: the RPQ query, 
   * and the global maps of predicate and attribute names to integers.
    */
  RpqAutomataAttr(const std::string &rpq, 
                  const unordered_map<std::string, uint64_t> &predicates,
                  const unordered_map<std::string, AttrInfo> &attributes);

   ~RpqAutomataAttr(void);

  /* Returns wether the NFA was correctly constructed from the RPQ
    */
  inline bool isValid() { return _isValid; };

  /* Returns a map with the predicates from the RPQ as keys, and the corresponding row in B as values
    */
  unordered_map<uint64_t, uint64_t> getB();

  /* Returns the set of active states after a transition from a given set of states by a given predicate 
       Params:
            - current: a mask representing the set of active states
            - predicate: the predicate used for transitioning to the next states
            - type: can be either FWD or BWD (see basics.h), and indicates whether to
              transition in a backward or forward fashion.
        Returns:
            A mask representing the set of states that are active after the transition
    */
  uint64_t next(uint64_t current, uint64_t predicate, int type);

  /* Returns whether a given set of states contain at least one final state of the automata
    */
  inline bool atFinal(uint64_t activeStates, int type)
  {
    return type == FWD
               ? activeStates & *(_nfaData->final)
               : activeStates & 1;
  }

  inline uint64_t getFinalStates()
  {
    return *(_nfaData->final);
  }

  inline bool anyActive(uint64_t activeStates)
  {
    return activeStates & *_B_any; 
  }

 /* Extracts square bracketed attribute constraints from a predicate and parses them,
  * storing the obtained acceptors to _AttrAcceptors. Returns the predicate name.
  * Expects the syntax 'attribute relation value' for conditions, 
  * for example '<orders[quantity >= 10][cost <= 25.99]>'.
  */
  std::string parse_attribute_conditions(const std::string& p_string, uint64_t automaton_states,
                                         const unordered_map<std::string, uint64_t>& predicates_map,
                                         const unordered_map<std::string, AttrInfo>& attributes_map);

 /* Returns the atuomaton's states where 
  * the automaton's attribute constraints related to predicat p are fulfilled,
  * using the attribute values at index i in the given attribute maps.
  */
  uint64_t states_with_matching_attributes(uint64_t i, uint64_t p,
                                           const vector<unordered_map<uint64_t, std::string>>& attributes_str,
                                           const vector<unordered_map<uint64_t, int>>& attributes_int,
                                           const vector<unordered_map<uint64_t, float>>& attributes_flt);
private:
  bool _isValid;
  regularData *_nfaData;
  unordered_map<uint64_t, Mask> _B;
  Mask _B_any;
 
  uint64_t fwdTrans(Mask *trans, int current, Mask predB);
  uint64_t bwdTrans(Mask *trans, int current, Mask predB);
  uint64_t next_1SliceInT(const regularData *nfaData, uint64_t current, uint64_t predicate, int type, Mask b);

  // Stores the attribute constraints and which states they apply to.
  // The outer vector is to be interpreted as a logical disjunction, 
  // while the inner vector represents a conjunction.
  vector<vector<std::tuple< uint64_t, AttrInfo, Acceptor >>>  _AttrAcceptors;
};


#endif

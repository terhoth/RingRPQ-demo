/*
 * File: HelloController.java
 * Author: modified by Terho Thorel
 * 	For the original code, see Github repository SSSVensk/XPathCypher
 * Description: Translation from XPath to syntax used by terhoth/Ring-RPQ-Attr.
 *  Parses the input with ANTLR4 and uses a listener to create the output. 
 *
 *  /translate takes an optional "prefixes" parameter: a JSON object mapping a
 *  namespace prefix to the URI it stands for, e.g.
 *    {"yago":"http://yago-knowledge.org/resource/"}
 *  When it is present, a prefixed name in the query (yago:Belgium) is expanded
 *  to the full URI, which is what the Ring-RPQ dictionaries are keyed by. The
 *  caller supplies it per graph (the backend reads it from graph_doc.json), so
 *  this service stays dataset-agnostic; omit it and translation behaves exactly
 *  as it did before prefixes existed.
 */

package com.example.demo;

import java.util.*;

import org.antlr.v4.runtime.*;
import org.antlr.v4.runtime.tree.*;
import java.util.List;

import org.json.simple.JSONObject;
import org.json.simple.parser.JSONParser;

import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RestController;
import org.springframework.web.bind.annotation.RequestParam;

@RestController
public class HelloController {

	@GetMapping("/translate")
	public JSONObject translateXPath(@RequestParam("query") String query,
	                                 @RequestParam(value = "prefixes", required = false) String prefixes) {

		try {

			// Parse the optional prefix map. A malformed one is reported rather
			// than ignored: silently dropping it would translate every prefixed
			// name to a bare local name that no dictionary can match, and the
			// resulting "Could not parse path endpoints" gives no hint why.
			final ArrayList<String> prefixErrors = new ArrayList<String>();
			final Map<String, String> prefixMap = parsePrefixes(prefixes, prefixErrors);
            
            // create a CharStream that reads from standard input
            ANTLRInputStream input = new ANTLRInputStream(query);

			System.out.println(query);

            // create a lexer that feeds off of input CharStream
            xpathRpqLexer lexer = new xpathRpqLexer(input);

            // create a buffer of tokens pulled from the lexer
            CommonTokenStream tokens = new CommonTokenStream(lexer);

            // create a parser that feeds off the tokens buffer
            xpathRpqParser parser = new xpathRpqParser(tokens);
			parser.removeErrorListeners();
			XPathErrorListener errorlistener = new XPathErrorListener();
			parser.addErrorListener(errorlistener);

			// parse
			XPathRpqListenerImpl listener = new XPathRpqListenerImpl(prefixMap);
			parser.addParseListener(listener);
            ParseTree tree = parser.main();    // begin parsing at rule main

			// print error messages
			ArrayList<String> errors = new ArrayList<String>();
			errors.addAll(prefixErrors);
			final ArrayList<String> errors1 = errorlistener.getErrorMessages();
			for (String msg : errors1) {
				System.out.println(msg);
			}
			errors.addAll(errors1);
			final ArrayList<String> errors2 = listener.getErrorMessages();
			for (String msg : errors2) {
				System.out.println(msg);
			}
			errors.addAll(errors2);
			
            
			// get translated query
			StringBuilder sb = new StringBuilder();
            final String rpq = sb.append(listener.getQuery()).toString();

			// create json
			JSONObject mainObject = new JSONObject();
			mainObject.put("query", rpq);			
			mainObject.put("errors", errors);

			System.out.println("");
			return mainObject;
            
    	} catch (Exception e) {
    		System.out.println("virhe!");
    		System.out.println(e);
			return new JSONObject();
    	}
	}

	// Reads the "prefixes" request parameter into a map. Absent or blank means
	// "no prefixes declared for this graph", which is not an error — it is the
	// normal case for every graph but yago.dat.
	private Map<String, String> parsePrefixes(String prefixes, ArrayList<String> errors) {
		Map<String, String> map = new LinkedHashMap<String, String>();
		if (prefixes == null || prefixes.trim().isEmpty()) {
			return map;
		}
		try {
			Object parsed = new JSONParser().parse(prefixes);
			if (!(parsed instanceof JSONObject)) {
				errors.add("Syntax error: the prefixes parameter must be a JSON object (0)");
				return map;
			}
			for (Object entry : ((JSONObject) parsed).entrySet()) {
				Map.Entry<?, ?> e = (Map.Entry<?, ?>) entry;
				if (e.getKey() == null || e.getValue() == null) continue;
				map.put(e.getKey().toString(), e.getValue().toString());
			}
		} catch (Exception e) {
			errors.add("Syntax error: could not parse the prefixes parameter (0)");
			map.clear();
		}
		return map;
	}
}

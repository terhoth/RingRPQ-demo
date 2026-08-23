package com.example.demo;

import java.util.*;

import org.antlr.v4.runtime.*;
import org.antlr.v4.runtime.tree.*;
import java.util.List;

import org.json.simple.JSONObject;

import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RestController;
import org.springframework.web.bind.annotation.RequestParam;

@RestController
public class HelloController {

	@GetMapping("/translate")
	public JSONObject translateXPath(@RequestParam("query") String query) {

		try {
            
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
			XPathRpqListenerImpl listener = new XPathRpqListenerImpl();
			parser.addParseListener(listener);
            ParseTree tree = parser.main();    // begin parsing at rule main

			// print error messages
			ArrayList<String> errors = new ArrayList<String>();
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
}

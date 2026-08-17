package com.example.demo;

import java.util.ArrayList;

import org.antlr.v4.runtime.Recognizer;
import org.antlr.v4.runtime.RecognitionException;
import org.antlr.v4.runtime.tree.TerminalNode;
import org.antlr.v4.runtime.BaseErrorListener;

public class XPathErrorListener extends BaseErrorListener {

	ArrayList<String> errors = new ArrayList<String>();

	
	public ArrayList<String> getErrorMessages() {
		return this.errors;
	}

	@Override
	public void syntaxError(Recognizer<?, ?> recognizer,
 	                        Object offendingSymbol,
 	                        int line, int charPositionInLine,
 	                        String msg,
 	                        RecognitionException e) {
 		//List<String> stack = ((Parser)recognizer).getRuleInvocationStack();
 		//Collections.reverse(stack);
 		//System.err.println("rule stack: " + stack);
		//System.err.println("line " + line + ":" + charPositionInLine + " at " +
		//                   offendingSymbol + ": " + msg);
		System.err.println("AAARGHH!");
		String message = "Syntax error: " + msg + " (" + charPositionInLine + ")";
		this.errors.add(message);
    }
 
}

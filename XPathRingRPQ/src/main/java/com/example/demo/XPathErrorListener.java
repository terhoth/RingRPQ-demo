package com.example.demo;

import java.util.ArrayList;

import org.antlr.v4.runtime.Recognizer;
import org.antlr.v4.runtime.RecognitionException;
import org.antlr.v4.runtime.tree.TerminalNode;
import org.antlr.v4.runtime.BaseErrorListener;

public class XPathErrorListener extends BaseErrorListener {

	private ArrayList<String> errors = new ArrayList<String>();

	
	public ArrayList<String> getErrorMessages() {
		return this.errors;
	}

	@Override
	public void syntaxError(Recognizer<?, ?> recognizer, Object offendingSymbol,
 	                        int line, int charPositionInLine,
 	                        String msg, RecognitionException e) {
		String message = "Syntax error: " + msg + " (" + charPositionInLine + ")";
		this.errors.add(message);
    }
 
}

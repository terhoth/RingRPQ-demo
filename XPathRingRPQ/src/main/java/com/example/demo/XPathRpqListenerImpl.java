/*
 * File: XPathRpqListener.java
 * Author: Terho Thorel, based on XPathCypherListener.java by SSSvensk
 * 	For the original code, see Github repository SSSVensk/XPathCypher
 * Description: ANTLR4 listener that constructs the RingRPQ query during parse.
 *  Also does some syntax and semantics checks, collecting a list of warning messages.
 *
 *  Optionally expands namespace-prefixed names: constructed with a non-empty
 *  prefix map, "yago:Belgium" becomes the full URI the Ring-RPQ dictionaries are
 *  keyed by. The map is supplied per graph by the caller (see HelloController),
 *  so nothing here is dataset-specific; with the default empty map a prefixed
 *  name keeps its historical behaviour of being reduced to its local part.
 * 
 */

package com.example.demo;

import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.LinkedList;
import java.util.Map;
import java.util.Queue;
import java.util.Stack;

import org.antlr.v4.runtime.tree.TerminalNode;


public class XPathRpqListenerImpl extends xpathRpqBaseListener {

	private StringBuilder query = new StringBuilder();
	private Stack<StringBuilder> queryStack = new Stack<StringBuilder>();

	private Stack<String> axis = new Stack<String>();
	private Stack<String> startEdge = new Stack<String>();
	private Stack<String> endEdge = new Stack<String>();

	// The local part of the nameTest being read, and the namespace prefix it was
	// written with (null when the name carried no prefix).
	private String nCName;
	private String nCNamePrefix;

	// Namespace prefix -> the URI it stands for. Empty means this graph declares
	// no prefixes, and no expansion is attempted.
	private final Map<String, String> prefixes;

	// Ring-RPQ-Attr's synthetic node-label attribute — NODELABEL_ATTR in
	// Ring-RPQ/src/parse_query_attr.cpp, registered for every graph by
	// load_index_util.cpp whether or not the index has an .A file. Its value is
	// the node's dictionary entry *including* the surrounding angle brackets.
	private static final String NODELABEL_ATTR = "__NODELABEL__";

	private boolean firstStep = true;
	private boolean deletedStep = false;

	private boolean firstStepWithPredicate = false;
	private boolean stepContainsPredicate = false;

	private boolean insideAttributeTest = false;
	private String attributeValue;

	private final boolean verbose = false;

	private ArrayList<String> errors = new ArrayList<String>();	
	private int i = 0;


	public XPathRpqListenerImpl() {
		this(Collections.<String, String>emptyMap());
	}

	public XPathRpqListenerImpl(Map<String, String> prefixes) {
		this.prefixes = (prefixes == null)
			? Collections.<String, String>emptyMap()
			: new LinkedHashMap<String, String>(prefixes);
	}


	public void setQuery(Object s) {
		this.query.append(s);
	}

	public Object getQuery() {
		return this.query;
	}

	public ArrayList<String> getErrorMessages() {
		return this.errors;
	}

	private void warnIf(boolean condition, String msg) {
		if (condition) {
			this.errors.add("Syntax warning: " + msg + " (" + this.i + ")");
		}
	}

	@Override
	public void visitTerminal(TerminalNode node) {
		this.i = node.getSymbol().getStopIndex();
	}

	@Override
	public void enterMain(xpathRpqParser.MainContext ctx) {
		this.queryStack.push(query);	
		this.axis.push("");
		this.startEdge.push("");
		this.endEdge.push("");
	}

	@Override
	public void exitMain(xpathRpqParser.MainContext ctx) {
		System.out.println("Ring query");
		this.query = this.queryStack.pop();
		System.out.println(this.query);
		System.out.println();
		System.out.println("Translation done");
	}

	@Override
	public void enterAndExpr(xpathRpqParser.AndExprContext ctx) {
		this.warnIf(ctx.parent.getChildCount() > 1, "Logical OR has not been implemented");
	}

	@Override 
	public void enterEqualityExpr(xpathRpqParser.EqualityExprContext ctx) {
		if (ctx.parent.getChildCount() > 1) {
			// separate and-combined expressions into different brackets
			// e.g. [@attr1 = 1 and @attr2 = 2] --> [@attr1 = 1][@attr2 = 2]
			if (this.verbose) System.out.println("enterEqualityExpr");
			this.warnIf(!this.insideAttributeTest, "Logical AND is not allowed outside attribute test");
			this.queryStack.peek().append("][");
			this.insideAttributeTest = false;
			if (this.verbose) System.out.println(this.queryStack.peek());
		}
	}

	@Override 
	public void exitEqualityExpr(xpathRpqParser.EqualityExprContext ctx) {
		if (ctx.getChildCount() > 1) {
			if (this.verbose) System.out.println("exitEqualityExpr");
			this.warnIf(this.attributeValue == null,
			            "Equality expressions must consist of attribute name, operand and value");
			this.queryStack.peek().append(" " + ctx.getChild(1) + " "); // operator
			if (this.attributeValue != null) {
				this.queryStack.peek().append(this.attributeValue.toString());
			}
			if (this.verbose) System.out.println(this.queryStack.peek());
		}
	}

	@Override
	public void enterRelationalExpr(xpathRpqParser.RelationalExprContext ctx) {
		this.warnIf(this.insideAttributeTest && ctx.parent.getChildCount() > 3, 
		            "Too many operands in equality expression");
		this.warnIf(!this.insideAttributeTest && ctx.parent.getChildCount() > 1, 
		            "Equality operators are not allowed outside attribute tests");
	}

	@Override 
	public void exitRelationalExpr(xpathRpqParser.RelationalExprContext ctx) {
		if (ctx.getChildCount() > 1) {
			if (this.verbose) System.out.println("exitRelationalExpr");
			this.queryStack.peek().append(" " + ctx.getChild(1) + " "); // operator
			this.queryStack.peek().append(this.attributeValue.toString());
			if (this.verbose) System.out.println(this.queryStack.peek());
			this.warnIf(!this.insideAttributeTest, 
			            "Relational expressions should begin with an @-prefixed attribute name");
		}
	}

	@Override
	public void enterAdditiveExpr(xpathRpqParser.AdditiveExprContext ctx) {
		this.warnIf(this.insideAttributeTest && ctx.parent.getChildCount() > 3, 
		            "Too many operands in relational expression");
		this.warnIf(!this.insideAttributeTest && ctx.parent.getChildCount() > 1, 
		            "Comparison operators are not allowed outside attribute tests");
	}

	@Override
	public void exitAdditiveExpr(xpathRpqParser.AdditiveExprContext ctx) {
		this.warnIf(ctx.getChildCount() > 1, "Addition has not been implemented");
	}

	@Override
	public void exitMultiplicativeExpr(xpathRpqParser.MultiplicativeExprContext ctx) {
		this.warnIf(ctx.getChildCount() > 1, "Multiplication has not been implemented");
	}

	@Override
	public void enterPathExprNoRoot(xpathRpqParser.PathExprNoRootContext ctx) {
		this.warnIf(ctx.parent.parent.getChildCount() > 1, "Union has not been implemented");
	}

	@Override
	public void exitPrimaryExpr(xpathRpqParser.PrimaryExprContext ctx) {
		if (this.verbose) System.out.println("exitPrimaryExpr");
		//Checking if the child node is leaf node
		if (ctx.getChild(0).getChild(0) == null) {
			String value = ctx.getChild(0).toString();
			this.warnIf(value.charAt(0) == '(', "Parenthezised expressions are not allowed");
			this.warnIf(value.charAt(0) != '(' && !this.insideAttributeTest, 
			            "Literals may be used only in attribute tests");
			if (value.matches("'.*'") || value.matches("\".*\"")) {
				this.attributeValue = value.substring(1, value.length()-1);
			} else {
				this.attributeValue = value;
			}
		} else {
			this.warnIf(true, "Functions and variables are unimplemented");
		}	
		if (this.verbose) System.out.println(this.queryStack.peek());
	}

	@Override
	public void enterRelativeLocationPath(xpathRpqParser.RelativeLocationPathContext ctx) {
		this.firstStep = true;
	}

	@Override
	public void exitRelativeLocationPath(xpathRpqParser.RelativeLocationPathContext ctx) {
		if (this.verbose) System.out.println("exitRelativeLocationPath");
		if (this.queryStack.size() == 1) {
			// this is the end of the main query
			int n_of_steps = (ctx.getChildCount() + 1) / 2;
			this.warnIf(n_of_steps <= 1, "The query must contain at least one edge!");
			this.warnIf(n_of_steps % 2 == 0, "The query must begin and end with a vertex");
			if (this.deletedStep || this.stepContainsPredicate) {
				// Either the end vertex is a wildcard, or
				// the last vertex contains a predicate, 
				// in which case a new variable end vertex needs to be added
				// because Ring-RPQ doesn't allow attributes on endpoints.
				this.queryStack.peek().append(" ?y");
			} else {
				// put a space before the last vertex, and remove &
				int i = this.queryStack.peek().lastIndexOf("/<&");
				if (i != -1) {
					this.queryStack.peek().delete(i, i+3);
					this.queryStack.peek().insert(i, " <");
				}
			}
		}
		this.firstStep = false;
		if (this.verbose) System.out.println(this.queryStack.peek());
	}

	@Override
	public void exitAxisSpecifier(xpathRpqParser.AxisSpecifierContext ctx) {
		if (this.verbose) System.out.println("exitAxisSpecifier");
		StringBuilder sb = new StringBuilder();
		sb.append(ctx.getChild(0));

		if (sb.toString().equals("parent") || sb.toString().equals("reverse")) {
			this.axis.push("parent");
			this.startEdge.push("<%");
			this.endEdge.push(">");
		} else if (sb.toString().equals("ancestor") || sb.toString().equals("reverse-transitive")) {
			this.startEdge.push("(<%");
			this.endEdge.push(">)+");
			this.axis.push("ancestor");
		} else if (sb.toString().equals("child")) {
			this.startEdge.push("<");
			this.endEdge.push(">");
			this.axis.push("child");
		} else if (sb.toString().equals("descendant") || sb.toString().equals("transitive")) {
			this.startEdge.push("(<");
			this.endEdge.push(">)+");
			this.axis.push("descendant");
		} else if (sb.toString().equals("ancestor-or-self") || sb.toString().equals("reverse-transitive-or-self")) {
			this.startEdge.push("(<%");
			this.endEdge.push(">)*");
			this.axis.push("ancestor-or-self");
		} else if (sb.toString().equals("descendant-or-self") || sb.toString().equals("transitive-or-self")) {
			this.startEdge.push("(<");
			this.endEdge.push(">)*");
			this.axis.push("descendant-or-self");
		} else if (sb.toString().equals("attribute") || sb.toString().equals("@")) {
			this.startEdge.push("");
			this.endEdge.push("");
			this.axis.push("attribute");
			this.insideAttributeTest = true;
		} else {
			this.warnIf(sb.toString().equals("namespace"), "Namespaces have not been implemented");
			this.warnIf(sb.toString().equals("following") || sb.toString().equals("following-sibling") ||
			            sb.toString().equals("preceding") || sb.toString().equals("preceding-sibling"), 
			            "Following/preceding axes have no interpretation in graphs");
			this.startEdge.push("<");
			this.endEdge.push(">");
			this.axis.push("child");
		}

		this.queryStack.peek().append(this.startEdge.peek());
		if (this.verbose) System.out.println(this.queryStack.peek());
	}

	@Override
	public void exitAbbreviatedStep(xpathRpqParser.AbbreviatedStepContext ctx) {
		this.warnIf(true, "Abbreviated steps have not been implemented");
	}

	@Override
	public void enterStep(xpathRpqParser.StepContext ctx) {
		this.warnIf(this.insideAttributeTest, 
		            "Attribute tests must consist of an attribute name, operator and value.");
		if (!this.firstStep && this.queryStack.peek().charAt(this.queryStack.peek().length()-1) != ' ') {
			if (this.verbose) System.out.println("enterStep");
			this.queryStack.peek().append("/");
			if (this.verbose) System.out.println(this.queryStack.peek());
		}
		if (this.queryStack.size() == 1) this.stepContainsPredicate = false;
		this.deletedStep = false;
	}

	@Override
	public void exitStep(xpathRpqParser.StepContext ctx) {
		if (this.verbose) System.out.println("exitStep");
		this.queryStack.peek().append(this.endEdge.peek());
	
		if (this.firstStep && this.queryStack.size() == 1) {
			// this is the very first vertex of the main query
			if (this.queryStack.peek().toString().equals("<&>")) {
				// replace unspecified start vertex with variable
				this.queryStack.pop();
				this.queryStack.push(new StringBuilder("?x"));
			} else {
				// remove & from constant start vertex
				this.queryStack.peek().deleteCharAt(1); 
			}
			this.queryStack.peek().append(" ");

		} else {
			// remove the step if it was just a nameless vertex
			String uselessStep = "/" + this.startEdge.peek() + "&" + this.endEdge.peek();
			int i = this.queryStack.peek().indexOf(uselessStep);
			if (i != -1) {
				this.queryStack.peek().delete(i, this.queryStack.peek().length());
				this.deletedStep = true;
			}
			// add underscore to nameless edges (also remove needless reversion)
			i = this.queryStack.peek().indexOf("<>");
			if (i != -1) {
				this.queryStack.peek().replace(i, i+2, "<_>");
			}
			i = this.queryStack.peek().indexOf("<%>"); 
			if (i != -1) {
				this.queryStack.peek().replace(i, i+3, "<_>");
			}
		}

		this.axis.pop();
		this.startEdge.pop();
		this.endEdge.pop();
		this.firstStep = false;
		if (this.verbose) System.out.println(this.queryStack.peek());
	}

	@Override
	public void enterNameTest(xpathRpqParser.NameTestContext ctx) {
		this.nCName = "";
		this.nCNamePrefix = null;
	}

	@Override
	public void exitNameTest(xpathRpqParser.NameTestContext ctx) {
		if (this.verbose) System.out.println("exitNameTest");
		int n_of_steps = (ctx.parent.parent.parent.getChildCount() + 1) / 2;
		// main query begins with a vertex; subqueries (=predicates) begin with an edge
		boolean isVertex = (this.queryStack.size() == 1 && n_of_steps % 2 == 1) ||
		                   (this.queryStack.size() >  1 && n_of_steps % 2 == 0);
		if (isVertex) {
			this.queryStack.peek().append('&');
		}
		// The grammar allows three shapes: '*', 'prefix:*' (three children), and
		// a qName that is either 'local' or 'prefix:local'.
		if (ctx.getChildCount() > 1) {
			this.appendNamespaceWildcard(isVertex);
		} else {
			this.queryStack.peek().append(this.resolveName());
		}
		if (this.verbose) System.out.println(this.queryStack.peek());
	}

	// 'prefix:*' — a wildcard restricted to one namespace. For a vertex this is
	// expressed as a node-label test, which is the one place the namespace
	// survives into the RPQ rather than being dropped.
	private void appendNamespaceWildcard(boolean isVertex) {
		// No prefix map for this graph: keep the historical reading of 'a:*' as
		// a step named 'a', so a graph that declares no prefixes translates
		// byte-for-byte as it always has.
		if (this.prefixes.isEmpty()) {
			this.warnIf(true, "Namespace identifier discarded");
			this.queryStack.peek().append(this.nCName);
			return;
		}

		String namespace = this.prefixes.get(this.nCName);
		if (namespace == null) {
			this.errors.add("Syntax error: unknown namespace prefix '"
			                + this.nCName + ":' (" + this.i + ")");
			return;
		}

		// Edge labels are predicates, not attributes, so there is nothing for a
		// namespace to be tested against — leave an unrestricted wildcard, which
		// exitStep turns into '<_>'.
		if (!isVertex) {
			this.warnIf(true, "A namespace cannot restrict an edge wildcard; '"
			                  + this.nCName + ":*' is treated as '*'");
			return;
		}

		// Ring-RPQ can't carry a predicate on a path endpoint, so a test on the
		// very first vertex needs an explicit variable in front of it — the same
		// move enterPredicate makes.
		if (this.firstStep && this.queryStack.size() == 1) {
			StringBuilder tmpStep = this.queryStack.pop();
			this.queryStack.push(new StringBuilder("?x ").append(tmpStep));
			// And, exactly as enterPredicate/exitPredicate arrange, the step must
			// stop counting as the first one. Otherwise exitStep's first-vertex
			// branch strips the 'x' straight back off and appends a space where
			// the next step needs a '/', which silently matches nothing.
			this.firstStep = false;
		}

		// The value is anchored with '<' because a node's __NODELABEL__ is its
		// dictionary entry including the angle brackets; without it the test is
		// never satisfied and the query returns no rows at all.
		this.queryStack.peek().append("[").append(NODELABEL_ATTR)
		                      .append(" BEGINS <").append(namespace).append("]");
		// The endpoint itself now has to become a variable — see
		// exitRelativeLocationPath, which reads this flag.
		this.stepContainsPredicate = true;
	}

	// Turns a named nameTest into the text that goes inside the step's angle
	// brackets.
	private String resolveName() {
		// An unprefixed name is passed through untouched, whether or not this
		// graph declares prefixes: expanding bare names by default would change
		// what existing queries mean.
		if (this.nCNamePrefix == null) {
			return this.nCName;
		}

		// No prefix map for this graph: keep the historical behaviour of
		// dropping the namespace rather than failing outright.
		if (this.prefixes.isEmpty()) {
			this.warnIf(true, "Namespace identifier discarded");
			return this.nCName;
		}

		String namespace = this.prefixes.get(this.nCNamePrefix);
		if (namespace == null) {
			this.errors.add("Syntax error: unknown namespace prefix '"
			                + this.nCNamePrefix + ":' (" + this.i + ")");
			return this.nCName;
		}

		// The angle brackets around the name are added by the step itself, and
		// the dictionaries are keyed by "<" + full URI + ">", so the bare
		// expanded URI is exactly what belongs here.
		return namespace + this.nCName;
	}

	@Override
	public void exitNCName(xpathRpqParser.NCNameContext ctx) {
		String name = new StringBuilder().append(ctx.getChild(0)).toString();
		// Within one nameTest, a second NCName means the first was a namespace
		// prefix — qName is "nCName (':' nCName)?".
		if (this.nCName != null && !this.nCName.isEmpty()) {
			this.nCNamePrefix = this.nCName;
		}
		this.nCName = name;
	}

	@Override
	public void enterPredicate(xpathRpqParser.PredicateContext ctx) {
		if (this.verbose) System.out.println("enterPredicate");
		if (this.firstStep && this.queryStack.size() == 1) {
			// As Ring-RPQ can't have predicates in path endpoints, 
			// add variable vertex ?x to the beginning
			StringBuilder tmpStep = this.queryStack.pop();
			this.queryStack.push(new StringBuilder("?x ").append(tmpStep));
		}
		int n_of_steps = (ctx.parent.parent.getChildCount() + 1) / 2;
		this.warnIf(this.insideAttributeTest, "Attribute tests may not contain predicates");
		this.firstStep = true; // first inside this predicate
		this.stepContainsPredicate = true; // the main query's step contains this predicate
		this.insideAttributeTest = false; // until proven otherwise
		this.queryStack.push(new StringBuilder());
		if (this.verbose) System.out.println(this.queryStack.peek());
	}

	@Override
	public void exitPredicate(xpathRpqParser.PredicateContext ctx) {
		if (this.verbose) System.out.println("exitPredicate");
		StringBuilder predicate = this.queryStack.pop();
		this.firstStep = false;
		this.warnIf(this.queryStack.isEmpty(), "The entire query seems to be inside a predicate??");
		if (this.insideAttributeTest) {
			this.insideAttributeTest = false;
			this.queryStack.peek().append("[");
			this.queryStack.peek().append(predicate.toString());
			this.queryStack.peek().append("]");
		} else {
			this.queryStack.peek().append("{");
			this.queryStack.peek().append(predicate.toString());
			this.queryStack.peek().append("}");
		}
		if (this.verbose) System.out.println(this.queryStack.peek());
	}

}

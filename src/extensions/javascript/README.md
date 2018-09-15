The JavaScript extension is designed to be used with the "Emscripten" build
of Ren-C, which can run in a web browser or with Node.js:

http://kripken.github.io/emscripten-site/

This extension is not required to use the JavaScript version of the libRebol
API, if the only interest is calling Rebol evaluations from JavaScript.
What it's for is the reverse direction--so that once Rebol code is running,
that it can call *back into* JavaScript.

The key way this is done is using "JavaScript natives".  A JavaScript native
is a function whose spec is a Rebol block--much like other functions.  But
the body is a string of JavaScript source.  

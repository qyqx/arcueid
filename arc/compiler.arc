;; Copyright (C) 2009 Rafael R. Sevilla
;;
;; This file is part of Arcueid
;;
;; Arcueid is free software; you can redistribute it and/or modify it
;; under the terms of the GNU Lesser General Public License as
;; published by the Free Software Foundation; either version 3 of the
;; License, or (at your option) any later version.
;;
;; This library is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU Lesser General Public License for more details.
;;
;; You should have received a copy of the GNU Lesser General Public
;; License along with this library; if not, see <http://www.gnu.org/licenses/>
;;

(def compile-toplevel (nexpr ctx env cont)
  (compile nexpr ctx env cont)
  (generate ctx 'ihlt))

;; Compile an expression with all special syntax expanded.
(def compile (nexpr ctx env cont)
  (let expr (macex nexpr)
    (if (literalp expr) (compile-literal expr ctx cont)
	(isa expr 'sym) (compile-ident expr ctx env cont)
	(isa expr 'cons) (compile-list expr ctx env cont)
	(compile-error "invalid expression" expr))))

;; Compile a literal value.
(def compile-literal (expr ctx cont)
  (do (if (no expr) (generate ctx 'inil)
	  (is expr t) (generate ctx 'itrue)
	  (fixnump expr) (generate ctx 'ildi (+ (* expr 2) 1))
	  (generate ctx 'ildl (find-literal expr ctx)))
      (compile-continuation ctx cont)))

(def compile-ident (expr ctx env cont)
    (if (ssyntax expr)
	(compile (ssexpand expr) ctx env cont)
	(do (let (found level offset) (find-var expr env)
	      (if found (generate ctx 'ilde level offset)
		  (generate ctx 'ildg (find-literal expr ctx))))
	    (compile-continuation ctx cont))))
  
(def compile-list (expr ctx env cont)
  (do (aif (spform (car expr)) (it expr ctx env cont)
	   (inline-func (car expr)) (it expr ctx env cont)
	   (compile-apply expr ctx env cont))
      (compile-continuation ctx cont)))

(def spform (ident)
  (case ident
    if compile-if
    fn compile-fn
    quote compile-quote
    quasiquote compile-quasiquote
    assign compile-assign))

(def inline-func (ident)
  (case ident
    cons (fn (expr ctx env cont) (compile-inline 'iconsr 2 expr ctx env cont))
    car (fn (expr ctx env cont) (compile-inline 'icar 1 expr ctx env cont))
    cdr (fn (expr ctx env cont) (compile-inline 'icdr 1 expr ctx env cont))
    scar (fn (expr ctx env cont) (compile-inline 'iscar 2 expr ctx env cont))
    scdr (fn (expr ctx env cont) (compile-inline 'iscdr 2 expr ctx env cont))
    is (fn (expr ctx env cont) (compile-inline 'iis 2 expr ctx env cont))
    + (fn (expr ctx env cont) (compile-inlinen 'iadd expr ctx env cont 0))
    - (fn (expr ctx env cont) (compile-inlinen2 'isub expr ctx env cont 0))
    * (fn (expr ctx env cont) (compile-inlinen 'imul expr ctx env cont 1))
    / (fn (expr ctx env cont) (compile-inlinen2 'idiv expr ctx env cont 1))))

(def compile-inline (instr narg expr ctx env cont)
  ((afn (xexpr count)
     (if (and (<= count 0) (no xexpr)) nil
	 (no expr) (compile-error "procedure " (car expr) " expects " narg
				  " arguments")
	 (do (compile (car xexpr) ctx env cont)
	     (if (no (cdr xexpr)) nil
		 (generate ctx 'ipush))
	     (self (cdr xexpr) (- count 1))))) (cdr expr) narg)
  (generate ctx instr)
  (compile-continuation ctx cont))

(def compile-inlinen (instr expr ctx env cont base)
  (compile base ctx env cont)
  (walk (cdr expr) [do (generate ctx 'ipush)
		       (compile _ ctx env cont)
		       (generate ctx instr)])
  (compile-continuation ctx cont))

(def compile-inlinen2 (instr expr ctx env cont base)
  (withs (xexpr (cdr expr) xelen (len xexpr))
    (if (is xelen 0) (compile-error (car expr) ": expects at least one argument, given 0")
	(is xelen 1) (compile-inlinen instr expr ctx env cont base)
	(compile-inlinen instr (cons (car expr) (cdr xexpr)) ctx env cont
			 (car xexpr)))))

(def compile-if (expr ctx env cont)
  (do
    ((afn (args)
       (if (no args) (generate ctx 'inil)
	   ;; compile tail end if no additional
	   (no:cdr args) (compile (car args) ctx env nil)
	   (do (compile (car args) ctx env nil)
	       (let jumpaddr (code-ptr ctx)
		 (generate ctx 'ijf 0)
		 (compile (cadr args) ctx env nil)
		 (let jumpaddr2 (code-ptr ctx)
		   (generate ctx 'ijmp 0)
		   (code-patch ctx (+ jumpaddr 1) (- (code-ptr ctx) jumpaddr))
		   ;; compile the else portion
		   (self (cddr args))
		   ;; Fix target address of jump
		   (code-patch ctx (+ jumpaddr2 1)
			       (- (code-ptr ctx) jumpaddr2)))))))
     (cdr expr))
    (compile-continuation ctx cont)))

(def compile-fn (expr ctx env cont)
  (withs (args (cadr expr) body (cddr expr) nctx (compiler-new-context)
	       nenv (compile-args args nctx env))
    ;; The body of a fn works as an implicit do/progn
    (map [compile _ nctx nenv nil] body)
    (compile-continuation nctx t)
    ;; Convert the new context into a code object and generate
    ;; an instruction in the present context to load it as a
    ;; literal, and then create a closure using the code object
    ;; and the current environment.
    (let newcode (context->code nctx)
      (generate ctx 'ildl (find-literal newcode ctx))
      (generate ctx 'icls)
      (compile-continuation ctx cont))))

;; This generates code to set up the new environment given the arguments.
;; After producing the code to generate the new environment, which
;; generally consists of an env instruction to create an environment
;; of the appropriate size and mvargs/mvoargs/mvrargs to move data from
;; the stack into the appropriate environment slot as well as including
;; instructions to perform any destructuring binds, return the new
;; environment which includes all the names specified properly ordered.
;; so that a call to find-var with the new environment can find the
;; names.
(def compile-args (args ctx env)
  (if (no args) env   ; just return the current environment if no args
      ;; If args is a single name, make an environment with a single
      ;; name and a list containing the name of the sole argument.
      (atom args)
      (do (generate ctx 'ienv 1)
	  (generate ctx 'imvrarg 0)
	  (add-env-frame (cons args nil) env))
      ;; If args is a list, we have to recurse into the list to
      ;; obtain the number of arguments specified, the names
      ;; of the arguments to bind, and the name to which the rest
      ;; arguments are bound, if any.
      ;; 
      ;; The names here may be either symbols which denote the
      ;; names of the actual arguments, or pairs whose car is the
      ;; name followed by the instructions needed to get at the
      ;; named value given the list argument.  An optional argument
      ;; is also represented as a pair, however it has the symbol o
      ;; followed by the argument name.
      (let (nargs names rest)
	  ((afn (args count names)
	     (if (no args) (list count (rev names) nil)
		 ;; If we see an atom, then we return the current count,
		 ;; the reversed list of the names, and the name of the
		 ;; argument which is to be where the rest argument is
		 ;; bound.
		 (atom args) (list (+ 1 count) (rev (cons args names)) t)
		 ;; If the argument specified is a cons cell, there
		 ;; are two possibilities, either it is an optional
		 ;; argument, or a destructuring bind.  In the latter
		 ;; case, we generate instructions that will extract
		 ;; the value of each element in the destructuring
		 ;; bind and use those.
		 (and (isa (car args) 'cons) (isnt (caar args) 'o))
		 (let dsb (dsb-list (car args))
		   (self (cdr args) (+ (len dsb) count)
			 (join (rev dsb) names)))
		 (self (cdr args) (+ 1 count)
		       (cons (car args) names))))
	   args 0 nil)
	;; Create a new environment frame of the appropriate size
	(generate ctx 'ienv nargs)
	;; Generate instructions to bind the values of the
	;; of the arguments to the environment.
	(let realnames
	    ((afn (arg count rest rnames)
	       ;; Generate a rest argument and bind
	       (if (and (no (cdr arg)) rest)
		   (do (generate ctx 'imvrarg count)
		       (rev (cons (car arg) rnames)))
		   (no arg) (rev rnames) ; done
		   ;; An optional argument.
		   (and (isa (car arg) 'cons) (is (caar arg) 'o))
		   (let oarg (car arg)
		     (generate ctx 'imvoarg count)
		     ;; To handle default parameters
		     (if (cddr oarg)
			 (do (generate ctx 'ilde 0 count)
			     ;; If we have a default value, fill it in
			     ;; if necessary.
			     (let jumpaddr (code-ptr ctx)
			       (generate ctx 'ijt 0)
			       (compile (car:cddr oarg) ctx env nil)
			       (generate ctx 'ipush)
			       (generate ctx 'imvoarg count)
			       (code-patch ctx (+ jumpaddr 1)
					   (- (code-ptr ctx)
					      jumpaddr)))))
		     (self (cdr arg) (+ 1 count) rest
			   (cons (cadr oarg) rnames)))
		   ;; Destructuring bind argument.
		   (and (isa (car arg) 'cons) (isnt (caar arg) 'o))
		   (do (map [generate ctx _] (cdr:car arg))
		       (generate ctx 'ipush)
		       (generate ctx 'imvarg count)
		       ;; Check if this is the last destructuring bind
		       ;; in the group.  If so, generate a pop instruction
		       ;; to discard the argument on the stack.
		       (let next (cadr arg)
			 (if (no (and (isa next 'cons) (isnt (car next) 'o)))
			     (generate ctx 'ipop)))
		       (self (cdr arg) (+ 1 count) rest
			     (cons (caar arg) rnames)))
		   (do (generate ctx 'imvarg count)
		       (self (cdr arg) (+ 1 count) rest
			     (cons (car arg) rnames)))))
	     names 0 rest nil)
	;; Create a new environment frame
	(add-env-frame realnames env)))))

;; Unroll and generate instructions for a destructuring bind of a list.
;; This function will return an assoc list of each element in the
;; original list followed by a list of car/cdr instructions that are
;; needed to get at that particular value given a copy of the original
;; list.  XXX: This is a rather naive algorithm, and we can probably
;; do better by generating code to load and store all values as we visit
;; them while traversing the conses, but I think it will do nicely for
;; a simple interpreter.
(def dsb-list (list)
  (let dsbinst
      ((afn (list instr ret)
	 (if (no list) ret
	     (isa list 'cons) (join
				(self (car list)
				      (cons 'icar instr) ret)
				(self (cdr list)
				      (cons 'icdr instr) ret))
	     (cons (cons list (cons 'idup (rev instr))) ret))) list nil nil)
    dsbinst))

;; This function works for compiling quoted as well as quasiquoted
;; expressions.  If the qq parameter is true, it will recognize
;; unquote and unquote-splicing expressions and treat them as appropriate.
(def compile-quote (expr ctx env cont (o qq))
  ((afn (arg)
     (if (and qq (isa arg 'cons) (is (car arg) 'unquote))
	 ;; To unquote something, all we need to do is evaluate it.  This
	 ;; generates a value becomes part of the result when recombined.
	 (do (compile (cadr arg) ctx env cont)
	     nil)
	 (and qq (isa arg 'cons) (is (car arg) 'unquote-splicing))
	 (do (compile (cadr arg) ctx env cont)
	     t)
	 (isa arg 'cons)
	 ;; If we see a cons, we need to recurse into the cdr of the
	 ;; argument first, generating the code for that, then push
	 ;; the result, then generate the code for the car of the
	 ;; argument, and then generate code to cons them together,
	 ;; or splice them together if the return so indicates.
	 (do (self (cdr arg))
	     (generate ctx 'ipush)
	     (if (self (car arg))
		 (generate ctx 'ispl)
		 (generate ctx 'icons))
	     nil)
	 ;; All other elements are treated as literals
	 (do (compile-literal arg ctx cont)
	      nil))) (cadr expr)))

;; Compile a quasiquoted expression.  This is not so trivial.
;; Basically, it requires us to reverse the list that is to be
;; quasiquoted, and then each element in its turn is quoted and consed
;; until the end of the list is encountered.  If an unquote is
;; encountered, the contents of the unquote are compiled, so that the
;; top of stack contains the results, and this too is consed to the
;; head of the list.  If an unquote-splicing is encountered, the
;; contents are also compiled, but the results are spliced.  The
;; reversing is necessary so that when the lists are consed together
;; they appear in the correct order.
(def compile-quasiquote (expr ctx env cont)
  (compile-quote expr ctx env cont t))

(def compile-apply (expr ctx env cont)
  (with (fname (car expr) args (cdr expr))
    (generate ctx 'icont 0)
    (let contaddr (- (code-ptr ctx) 1)
      (walk (rev args) [do (compile _ ctx env nil)
			   (generate ctx 'ipush)])
      (compile fname ctx env nil)
      (generate ctx 'iapply (len args))
      (code-patch ctx contaddr (+ (- (code-ptr ctx) contaddr) 1))
      (compile-continuation ctx cont))))

;; Compile an assign special form.  Called in previous versions of Arc
;; set, the assign form takes symbol-value pairs in its argument and
;; assigns them.
(def compile-assign (expr ctx env cont)
  ((afn (x)
     (if (no x) nil
	 (with (a (macex (car x)) val (cadr x))
	   (compile val ctx env cont)
	   (if (no a) (compile-error "Can't rebind nil")
	       (is a 't) (compile-error "Can't rebind t")
	       (let (found level offset) (find-var a env)
		 (if found (generate ctx 'iste level offset)
		     (generate ctx 'istg (find-literal a ctx)))))
	   (self (cddr x))))) (cdr expr))
  (compile-continuation ctx cont))

(def compile-continuation (ctx cont)
  (if cont (generate ctx 'iret) ctx))

(def literalp (expr)
  (or (no expr)
      (is expr t)
      (isa expr 'char)
      (isa expr 'string)
      (isa expr 'int)
      (isa expr 'num)))

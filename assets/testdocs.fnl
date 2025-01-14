(local fennel (require :fennel))

(var *buffer* "")

(fn printstr [...]
  (set *buffer*
       (.. *buffer* (accumulate [s "" _ e (ipairs arg)] (.. s e)) "\n")))

(G.window.set_dimensions 1440 1080)
(G.graphics.clear)
(G.graphics.set_color :white)
(each [lib-name lib-docs (pairs _Docs)]
  (printstr "Docs for " lib-name)
  (printstr "\n")
  (each [fn-name fn-docs (pairs lib-docs)]
    (printstr :G. lib-name "." fn-name "\n\n\t" fn-docs.docstring "\n")
    (each [_ {:name arg-name :docstring arg-doc} (ipairs fn-docs.args)]
      (printstr "\t\t@arg " arg-name ": " arg-doc))
    (printstr "\tReturns: ")
    (each [_ ret-doc (ipairs fn-docs.returns)]
      (printstr "\t\t@return " ret-doc))
    (printstr "\n")))

(G.graphics.draw_text :terminus.ttf 24 *buffer* 0 24)

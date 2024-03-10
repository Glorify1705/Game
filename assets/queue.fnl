(local Object (require :classic))

(local Queue (Object:extend))

(fn Queue.new [q]
  (tset q :elements [])
  (tset q :first 1)
  (tset q :last 0))

(fn Queue.empty? [q] (< q.last q.first))

(fn Queue.peek [q] (. q.elements q.first))

(fn Queue.pop! [q] (tset q :first (+ q.first 1)))

(fn Queue.push! [q e] (table.insert q.elements e) (tset q :last (+ q.last 1)))

Queue

(local queue {})

(fn queue.create []
  {:elements [] :first 1 :last 0})

(fn queue.empty? [q] (< q.last q.first))

(fn queue.peek [q] (. q.elements q.first))

(fn queue.pop! [q] (tset q :first (+ q.first 1)))

(fn queue.push! [q e] (table.insert q.elements e) (tset q :last (+ q.last 1)))

queue

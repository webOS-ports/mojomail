// A reference to a container element, taken from an iterator, still used
// after that element was erased. erase() frees the node (set, map, list) or
// shifts the next element into the slot (vector, deque): the reference then
// points at freed memory or at another element. What crashed
// ActivitySet::EndSomeActivities and CommandManager::CommandComplete.
// Take a copy (const T x = *it;) before erasing instead.
@r@
type T;
identifier ref;
expression it, c;
@@
(
  const T& ref = *it;
|
  T& ref = *it;
)
  <+... c.erase(it) ...+>
* ref

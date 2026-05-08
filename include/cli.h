#ifndef VROUTER_CLI_H
#define VROUTER_CLI_H

namespace vrouter {

class Router;

// Runs the interactive CLI loop. Reads commands from std::cin, writes
// to std::cout. Returns when the user types 'quit' or 'exit', or when
// EOF is reached on stdin (Ctrl-D, or stdin redirected from a file
// that's exhausted).
//
// Errors in command execution are reported to std::cout but do not
// terminate the loop — the user gets a fresh prompt.
void run_cli(Router& router);

}  // namespace vrouter

#endif  // VROUTER_CLI_H

// Supplies the argv contract expected by litmus7's generated entry point.
// SPDX-License-Identifier: Apache-2.0
extern int litmus_generated_main(int argc, char **argv);

int main(void)
{
  char program[] = "litmus";
  char *argv[] = {program, 0};
  return litmus_generated_main(1, argv);
}

#!/usr/bin/env python3
# Compares source-mapped simulation IR stages and ranks duplicated work in a fixed compiled plan.
import argparse
import collections
import json
import pathlib
import re
import subprocess


def function_end(source, start):
    depth = 0
    tokens = re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|[{}]', source[start:], re.S)
    for token in tokens:
        if token[0] == '{':
            depth += 1
        elif token[0] == '}':
            depth -= 1
            if depth == 0:
                return start + token.end()
    raise ValueError('generated command has no complete function body')



def inspect_machine_code(text):
    """Count static code and identify Zen 4's slow memory-destination compression."""
    opcode_counts, functions = collections.Counter(), collections.defaultdict(collections.Counter)
    registers = collections.defaultdict(set)
    hazards, symbol = [], '<unknown>'
    for line in text.splitlines():
        label = re.match(r'^[0-9a-f]+ <(.+)>:', line)
        if label:
            symbol = label[1]
            continue
        instruction = re.match(r'^\s*([0-9a-f]+):\s+([a-z][a-z0-9.]*)\s*(.*)', line)
        if not instruction:
            continue
        address, opcode, operands = instruction.groups()
        opcode_counts[opcode] += 1
        counts = functions[symbol]
        counts['instructions'] += 1
        vector = re.findall(r'\b[xyz]mm(\d+)\b', operands)
        registers[symbol].update(map(int, vector))
        counts['vector_instructions'] += bool(vector)
        counts['stack_memory_instructions'] += bool(re.search(r'\[[^]]*\b(?:rsp|rbp)\b', operands))
        counts['calls'] += opcode == 'call'
        # Intel syntax places the destination first. Register compression is
        # allowed; only memory destinations trigger the Zen 4 policy.
        if re.fullmatch(r'v(?:p)?compress[bwdq]|vcompressp[sd]', opcode) and '[' in operands.split(',')[0]:
            hazards.append(dict(function=symbol, address=address, opcode=opcode, operands=operands))
    return dict(note='Static instruction counts and referenced registers, not dynamic costs or peak register liveness.',
                instructions=sum(opcode_counts.values()), opcodes=dict(opcode_counts),
                zen4_compress_stores=hazards,
                functions=[dict(function=name, **counts, referenced_vector_registers=sorted(registers[name]))
                           for name, counts in sorted(functions.items(), key=lambda x: x[1]['instructions'], reverse=True)])


def inspect_binary(path):
    return inspect_machine_code(subprocess.check_output(
        ['objdump', '-d', '-M', 'intel', '--no-show-raw-insn', str(path)], text=True))


def graph_shapes(model):
    widths, names = model['values'], model['opcodes']
    depth, groups, muxes = {}, collections.defaultdict(list), []
    for code, out, args, imm in model['operations']:
        level = 1 + max((depth.get(a, 0) for a in args), default=0)
        depth[out] = level
        if names[code] in ('not', 'and', 'or', 'xor', 'add', 'sub', 'eq', 'ult', 'slt'):
            lane = max([widths[out], *[widths[a] for a in args]])
            if lane <= 64:
                groups[(level, names[code], lane, widths[out])].append(out)
        if names[code] == 'mux_lookup':
            seen, active = set(), []
            words = (widths[args[0]] + 63) // 64
            for i, value in enumerate(args[2:]):
                key = tuple(imm[i*words:(i+1)*words])
                if key not in seen and value != args[1]:
                    active.append(value)
                seen.add(key)
            muxes.append(dict(output=out, selector=args[0], selector_width=widths[args[0]],
                              output_width=widths[out], explicit_rows=len(args)-2,
                              nondefault_rows=len(active), distinct_nondefault_values=len(set(active))))
    parallel = [dict(level=k[0], opcode=k[1], lane_width=k[2], output_width=k[3],
                     operations=len(v), outputs=v) for k, v in groups.items() if len(v) >= 4]
    return dict(note='Equal-depth pure operations are independent; gathering, demand guards and longer live ranges still determine profitability.',
                max_dependency_depth=max(depth.values(), default=0),
                mux_rows=dict(collections.Counter(str(m['explicit_rows']) for m in muxes)),
                redundant_mux_rows=sum(m['explicit_rows']-m['nondefault_rows'] for m in muxes),
                reducible_muxes=sorted((m for m in muxes if m['explicit_rows'] > m['nondefault_rows']),
                                      key=lambda m: m['explicit_rows']-m['nondefault_rows'], reverse=True)[:40],
                parallel_groups=sorted(parallel, key=lambda g: g['operations'], reverse=True)[:40])


def summarize(model):
    ops = model['operations']
    names = model['opcodes']
    widths = model['values']
    origins = collections.defaultdict(list)
    for value, occurrence, source, name, opcode, low, width in model.get('origins', []):
        origins[value].append(dict(occurrence=occurrence, source=source, name=name,
                                   opcode=opcode, low=low, width=width))
    selectors = collections.defaultdict(list)
    views = collections.defaultdict(set)
    for code, out, args, imm in ops:
        if code == 15:
            selectors[(args[0], tuple(imm))].append(out)
        for origin in origins[out]:
            views[(origin['occurrence'], origin['source'])].add(out)
    candidates = []
    for (selector, keys), outputs in selectors.items():
        if len(outputs) < 2:
            continue
        key_count = len(keys) // ((widths[selector] + 63) // 64)
        candidates.append(dict(selector=selector, selector_width=widths[selector],
                               lookup_count=len(outputs), key_count=key_count,
                               worst_case_comparisons=len(outputs)*key_count,
                               outputs=outputs, output_widths=[widths[x] for x in outputs],
                               origins=origins[outputs[0]][:4]))
    candidates.sort(key=lambda x: x['worst_case_comparisons'], reverse=True)
    decoders = collections.defaultdict(list)
    for code, out, args, imm in ops:
        if code == 25 and widths[args[0]] <= 8 and widths[out] <= 64:
            decoders[(widths[args[0]], widths[out], tuple(imm))].append(out)
    tables = [dict(selector_width=key[0], output_width=key[1], rows=key[2][0],
                   occurrences=len(outputs), shared_table_bytes=(1 << key[0]) * next(size for size in [1, 2, 4, 8] if size*8 >= key[1]),
                   origins=origins[outputs[0]][:4]) for key, outputs in decoders.items()]
    return dict(operations=len(ops), values=len(widths),
                opcodes=dict(collections.Counter(names[o[0]] for o in ops)),
                one_bit_operations=sum(widths[o[1]] == 1 for o in ops),
                logical_value_bits=sum(widths),
                unallocated_word_bytes=sum((w+63)//64*8 for w in widths),
                objects=len(model.get('objects', [])),
                matcher_work=matcher_work(model),
                graph_shapes=graph_shapes(model),
                duplicated_selectors=candidates[:30],
                small_decode_table_candidates=sorted(tables, key=lambda x: x['rows']*x['occurrences'], reverse=True)[:30],
                overlapping_source_views=sorted(
                    (dict(occurrence=k[0], source=k[1], views=len(v)) for k, v in views.items() if len(v)>1),
                    key=lambda x: x['views'], reverse=True)[:30])


def matcher_work(model):
    result = []
    for index, obj in enumerate(model.get('objects', [])):
        if obj[0] != 10:
            continue
        queries = [op for op in model['operations'] if op[0] == 29 and op[3][0] == index]
        steps = sum(1 if op[3][1] >= obj[2] else op[3][1]+1 for op in queries)
        result.append(dict(occurrence=obj[5], inputs=obj[1], outputs=obj[2], queries=len(queries),
                           grant_steps=steps, request_bit_reads=steps*obj[1],
                           explicit_prefix_steps=sum(op[3][1] >= obj[2] for op in queries)))
    return result


def inspect(original, optimized, plan=None, source=None, commands=None):
    report = dict(format='rhodium-ir-comparison-v1', original=summarize(original),
                  optimized=summarize(optimized),
                  inventory=optimized.get('inventory', []),
                  note='Comparison counts estimate structural work, not measured execution time; state and pure logic have different costs.')
    if plan:
        workers = [sum(len(b[2]) for b in worker['batches']) for worker in plan['workers']]
        instructions = [instruction for worker in plan['workers'] for batch in worker['batches'] for instruction in batch[2]]
        origins = collections.defaultdict(set)
        for value, occurrence, *_ in optimized.get('origins', []):
            origins[value].add(occurrence)
        report['plan'] = dict(worker_operations=workers,
                              replicated_operations=plan['replicated_operations'],
                              estimated_work=plan['estimated_work'], peak_work=plan['peak_work'],
                              scratch_words=max((offset+(width+63)//64 for width, offset, *_ in plan['values']), default=0),
                              object_owners=collections.Counter(str(o[4]) for o in plan['objects']),
                              register_owners=collections.Counter(str(r[1]) for r in plan.get('registers', [])),
                              matcher_work=matcher_work(plan),
                              source_value_copies={str(k): n for k, n in collections.Counter(v[3] for v in plan['values'] if len(v)>3).items() if n>1},
                              duplicated_computations={str(k): n for k, n in collections.Counter(i[4] for i in instructions if len(i)>4).items() if n>1})
    if source:
        report['generated'] = dict(source_bytes=len(source.encode()),
                                   functions=collections.Counter(re.findall(r'static (?:RDS_(?:NOINLINE|INLINE_SELECTION) )?(?:int|void) (bound|block|direct)_\d+\(', source)),
                                   operand_binding_reads=len(re.findall(r'(?:[vq]|\(\(uint8_t\*\)v\))\[b\[', source)),
                                   byte_stored_scalars=sum(map(int,re.findall(r'byte-stored scalar words: (\d+)',source))),
                                   direct_storage_selectors=len(re.findall(r'static const size_t refs\[\]=\{offsetof\(object_state,',source)),
                                   linear_selector_searches=len(re.findall(r'for\(uint32_t j=0;j<\d+;\+\+j\)if\(!cg_cmp', source)),
                                   linear_decode_searches=source.count('for(uint64_t i=0;i<im[0];++i,row+=2*an+n)'))
        if plan:
            # These command ranges are emitted by the compiler, and address the
            # same flattened instruction order reported by the load-time plan.
            markers = list(re.finditer(r'/\* command (\d+) lane (\d+) instructions (\d+)\.\.(\d+) \*/', source))
            mapped = []
            for command in markers:
                number, lane, begin, end = map(int, command.groups())
                stop = function_end(source, command.end())
                occurrences = collections.Counter(occurrence for instruction in instructions[begin:end]
                                                  if len(instruction)>4 for occurrence in origins[instruction[4]])
                mapped.append(dict(command=number, lane=lane, instruction_range=[begin, end],
                                   mapped_instructions=sum(len(instruction)>4 and bool(origins[instruction[4]]) for instruction in instructions[begin:end]),
                                   source_line=source.count('\n', 0, command.start())+1,
                                   source_bytes=stop-command.start(),
                                   source_occurrences=occurrences.most_common(8)))
            report['generated']['largest_commands'] = sorted(mapped, key=lambda x: x['source_bytes'], reverse=True)[:30]
            if commands:
                report['generated']['requested_commands'] = [command for command in mapped if command['command'] in commands]
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('original', type=pathlib.Path)
    parser.add_argument('optimized', type=pathlib.Path)
    parser.add_argument('--plan', type=pathlib.Path)
    parser.add_argument('--source', type=pathlib.Path)
    parser.add_argument('--binary', type=pathlib.Path, help='inspect x86 machine code and Zen 4 compress-store hazards')
    parser.add_argument('--command', type=int, action='append', help='map a profiled bound_N/direct_N function back to source occurrences; repeatable')
    parser.add_argument('--output', type=pathlib.Path)
    args = parser.parse_args()
    report = inspect(json.loads(args.original.read_text()), json.loads(args.optimized.read_text()),
                     json.loads(args.plan.read_text()) if args.plan else None,
                     args.source.read_text() if args.source else None, args.command)
    if args.binary:
        report['machine_code'] = inspect_binary(args.binary)
    text = json.dumps(report, indent=2)+'\n'
    if args.output:
        args.output.write_text(text)
    else:
        print(text, end='')


if __name__ == '__main__':
    main()

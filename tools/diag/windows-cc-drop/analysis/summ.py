import json,sys
for fn in sys.argv[1:]:
    j=json.load(open(fn))
    print("==",fn,j['mode'],j['status'])
    for r in j['rx']:
        ev=[e for e in r['events'] if not e[6]]
        print(f"{r['label']:3} {r['kind']} tsid={r['pat_tsid']:5} pk={r['packets_total']:7} deliv={r['packets_delivered']:7} null={r['nulls']:6} cc={r['cc_errors']:4} lost={r['est_lost']:4} dup={r['cc_dup']} tei={r['tei']:5} sync={r['sync_loss']} st_cc={r['startup_cc']} st_pk={r['startup_packets']} stab={r['stabilized_ms']:.0f} forced={r['stab_forced']} rc={r['exit_code']} cnr={r['cnr']:.2f} maxrem={r['max_remain']} {r['note']} | {r['stream_end']}")
        if ev and r['tei']<1000:
            print("     events:",[(round(e[0]),e[1],e[2],e[3]) for e in ev[:12]])

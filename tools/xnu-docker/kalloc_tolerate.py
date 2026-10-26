# Linux-build only (run from the XNU source root by build-in-container.sh): without Apple's
# kalloc_type builtins every type counts as "pointers and data", so code that allocates with
# kalloc_data and frees with kfree_type(uint8_t, ...) (subr_log_stream.c, the oslog stream device)
# names the default heap for an element of the data heap: "kfree: ... found in heap data.* instead
# of *" panics in diagnosticd. A data vs non-data disagreement is tolerated there; the element is
# freed into the zone it is in, as always. Every other heap confusion still panics.
p = 'osfmk/kern/kalloc.c'
s = open(p).read()
a = '''	zsflags = zone_security_config(z);
	if (kheap == KHEAP_DATA_BUFFERS) {
		if (kheap->kh_heap_id != zsflags.z_kheap_id) {
			kfree_heap_confusion_panic(kheap, data, size, z);
		}
	} else {
		if ((kheap->kh_heap_id != zsflags.z_kheap_id) &&
		    (zsflags.z_kheap_id != KHEAP_ID_SHARED)) {
			kfree_heap_confusion_panic(kheap, data, size, z);
		}
	}
'''
b = '''	zsflags = zone_security_config(z);
	bool data_confusion_tolerated = false;
#if XNU_NO_TYPE_BUILTINS
	/* see tools/xnu-docker/kalloc_tolerate.py */
	data_confusion_tolerated = (kheap == KHEAP_DATA_BUFFERS) !=
	    (zsflags.z_kheap_id == KHEAP_ID_DATA_BUFFERS);
#endif
	if (data_confusion_tolerated) {
		/* freed into its own zone below */
	} else if (kheap == KHEAP_DATA_BUFFERS) {
		if (kheap->kh_heap_id != zsflags.z_kheap_id) {
			kfree_heap_confusion_panic(kheap, data, size, z);
		}
	} else {
		if ((kheap->kh_heap_id != zsflags.z_kheap_id) &&
		    (zsflags.z_kheap_id != KHEAP_ID_SHARED)) {
			kfree_heap_confusion_panic(kheap, data, size, z);
		}
	}
'''
assert s.count(a) == 1, 'kalloc.c changed'
open(p, 'w').write(s.replace(a, b))
print('kalloc.c: data/default heap confusion tolerated without the builtins')

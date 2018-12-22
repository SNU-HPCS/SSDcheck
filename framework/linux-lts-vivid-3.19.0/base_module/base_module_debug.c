
// for debuging
static void dump_all_interval_list(struct seq_file *m, int devid);
static void dump_all_interval_dic(struct seq_file *m, int devid);
static void dump_all_interval_group(struct seq_file *m, int devid);
static void dump_all_poss_entry(struct seq_file *m, int devid);
static void dump_all(struct seq_file *m, int devid);

static void dump_all_interval_list(struct seq_file *m, int devid)
{
    struct interval_list_elem_t *list_elem;
    int line_count = 0;

    seq_printf(m, "[DEBUG] ============= dump all interval list =============\n");
    seq_printf(m, "[DEBUG] total count: %d\n",interval_list_head_arr[devid].total_count);
    seq_printf(m, "[DEBUG] interval: ");
    list_for_each_entry(list_elem,
            &interval_list_head_arr[devid].list, list) {
        line_count++;
        seq_printf(m, "[%2d] ",list_elem->interval);
        if (line_count % 10 == 0) {
            seq_printf(m, "\n");
            seq_printf(m, "                    ");
        }
    }
    seq_printf(m, "\n");
}

static void dump_all_interval_dic(struct seq_file *m, int devid)
{
    struct interval_dic_elem_t *dic_elem;

    seq_printf(m, "[DEBUG] ============= dump all inteval_dic =============\n");
    list_for_each_entry(dic_elem,
            &interval_dic_arr[devid], list) {
        seq_printf(m, "[DEBUG] interval: %3d, count: %d\n",dic_elem->interval, dic_elem->count);
    }
}

static void dump_all_interval_group(struct seq_file *m, int devid)
{
    struct interval_group_elem_t *group_elem;

    seq_printf(m, "[DEBUG] ============= dump all interval group =============\n");
    list_for_each_entry(group_elem,
            &interval_group_dic_arr[devid], list) {
        seq_printf(m, "[DEBUG] interval: (%3d - %3d), count: %d\n",
                group_elem->interval_start, group_elem->interval_end,
                group_elem->count);
    }
}

static void dump_all_poss_entry(struct seq_file *m, int devid)
{
    seq_printf(m, "[DEBUG] ============= dump poss entry =============\n");
    seq_printf(m, "[DEBUG] possibility:  %d\n",(int)poss_entry_arr[devid].possibility);
    seq_printf(m, "[DEBUG] devid:        %d\n",(int)poss_entry_arr[devid].devid);
    seq_printf(m, "[DEBUG] io_count:     %d\n",(int)poss_entry_arr[devid].io_count);
    seq_printf(m, "[DEBUG] cur_interval: %d\n",(int)poss_entry_arr[devid].cur_interval);
}

static void dump_all(struct seq_file *m, int devid)
{
    unsigned long flags;

    spin_lock_irqsave(&interval_group_lock_arr[devid], flags);

    dump_all_poss_entry(m, devid);
    dump_all_interval_list(m, devid);
    dump_all_interval_dic(m, devid);
    dump_all_interval_group(m, devid);

    spin_unlock_irqrestore(&interval_group_lock_arr[devid], flags);
}


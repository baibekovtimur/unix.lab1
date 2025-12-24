#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/time.h>

#define PROC_NAME "tsulab"

static int proc_entry_open(struct inode *inode, struct file *file)
{
    return single_open(file, lunar_progress_show, NULL);
}

static const struct proc_ops proc_entry_ops = {
    .proc_open = proc_entry_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static int __init time_module_init(void)
{
    if (!proc_create(PROC_NAME, 0444, NULL, &proc_entry_ops))
        return -ENOMEM;

    pr_info("Welcome to the Tomsk State University\n");
    return 0;
}

static void __exit time_module_exit(void)
{
    remove_proc_entry(PROC_NAME, NULL);
    pr_info("Tomsk State University forever!\n");
}

static int lunar_progress_show(struct seq_file *m, void *v)
{
    // Прошлый лунный новый год: 2025-01-29 00:00:00 UTC
    // Текущий лунный новый год: 2026-02-17 00:00:00 UTC

    const time64_t prev_lunar_year = 1738108800; // 2025-01-29
    const time64_t next_lunar_year = 1771286400; // 2026-02-17

    time64_t current_time = ktime_get_real_seconds();

    long long passed_time = 0;
    long long full_period = next_lunar_year - prev_lunar_year;
    long long progress_percent = 0;

    if (current_time <= prev_lunar_year)
    {
        progress_percent = 0;
    }
    else if (current_time >= next_lunar_year)
    {
        progress_percent = 100;
    }
    else
    {
        passed_time = current_time - prev_lunar_year;
        progress_percent = passed_time * 100 / full_period;
    }

    seq_printf(m, "Прошло около %lld%% времени между прошлым и текущем лунным Новым годом.\n",
               progress_percent);

    return 0;
}

module_init(time_module_init);
module_exit(time_module_exit);

MODULE_LICENSE("GPL");
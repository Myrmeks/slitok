#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <algorithm>
#include <vector>
#include <map>
#include <set>
#include <list>
#include <vector>
#include <pthread.h>
#include <unistd.h>
#include <sys/eventfd.h>

class Task
{
public:
  virtual ~Task() {}
  virtual void run() = 0;
  virtual void finalize() = 0;
};

class TaskThread
{
public:
  TaskThread(uint64_t emask, int efd);
  ~TaskThread();
  void start();
  void schedule(Task *task);
  void finalize();
  void stop();

  uint64_t m_emask;
private:
  enum TaskState
  {
    T_NONE,
    T_TASK,
    T_STOP
  };

  TaskState m_state;
  pthread_t m_thread;
  Task *m_task;
  pthread_mutex_t m_mutex;
  pthread_cond_t m_cond;
  int m_efd;

  static void *thread_proc(void *obj);
  void run();
};

TaskThread::TaskThread(uint64_t emask, int efd)
  : m_emask(emask),
    m_state(T_NONE),
    m_efd(efd)
{
  pthread_mutex_init(&m_mutex, 0);
  pthread_cond_init(&m_cond, 0);
}

TaskThread::~TaskThread()
{
  pthread_cond_destroy(&m_cond);
  pthread_mutex_destroy(&m_mutex);
}

void TaskThread::start()
{
  pthread_create(&m_thread, 0, thread_proc, this);
}

void TaskThread::schedule(Task *task)
{
  pthread_mutex_lock(&m_mutex);
  m_state = T_TASK;
  m_task = task;
  pthread_cond_broadcast(&m_cond);
  pthread_mutex_unlock(&m_mutex);
}

void TaskThread::finalize()
{
  m_task->finalize();
  delete m_task;
}

void TaskThread::stop()
{
  pthread_mutex_lock(&m_mutex);
  m_state = T_STOP;
  pthread_cond_broadcast(&m_cond);
  pthread_mutex_unlock(&m_mutex);
  pthread_join(m_thread, 0);
}

void *TaskThread::thread_proc(void *obj)
{
  ((TaskThread*)obj)->run();
  return 0;
}

void TaskThread::run()
{
  pthread_mutex_lock(&m_mutex);
  while (m_state != T_STOP)
  {
    while (m_state == T_NONE)
      pthread_cond_wait(&m_cond, &m_mutex);

    if (m_state == T_TASK)
    {
      m_task->run();
      m_state = T_NONE;
      write(m_efd, &m_emask, 8);
    }
  }
  pthread_mutex_unlock(&m_mutex);
}

class Dispatcher
{
public:
  Dispatcher(int thread_num);
  ~Dispatcher();
  void schedule(Task *task);
  void synchronize();

private:
  int m_efd;
  int m_thread_num;
  uint64_t m_free_threads;
  uint64_t m_thread_mask;
  std::vector<TaskThread*> m_threads;

  void wait();
};

Dispatcher::Dispatcher(int thread_num)
  : m_thread_num(thread_num)
{
  m_efd = eventfd(0, 0);
  m_thread_mask = ~((uint64_t)-1 << thread_num);
  m_free_threads = m_thread_mask;
  for (int i = 0; i < thread_num; ++i)
    m_threads.push_back(new TaskThread(1ULL << i, m_efd));
  for (int i = 0; i < thread_num; ++i)
    m_threads[i]->start();
}

Dispatcher::~Dispatcher()
{
  for (int i = 0; i < m_thread_num; ++i)
  {
    m_threads[i]->stop();
    delete m_threads[i];
  }
}

void Dispatcher::wait()
{
  uint64_t mask;
  read(m_efd, &mask, 8);
  m_free_threads |= mask;
  for (int i = 0; i < m_thread_num; ++i)
  {
    if (mask & m_threads[i]->m_emask)
      m_threads[i]->finalize();
  }
}

void Dispatcher::schedule(Task *task)
{
  while (!m_free_threads)
  {
    wait();
  }

  int th = 0;
  for (; th < m_thread_num; ++th)
  {
    if (m_free_threads & m_threads[th]->m_emask)
      break;
  }

  m_free_threads &= ~m_threads[th]->m_emask;
  m_threads[th]->schedule(task);
}

void Dispatcher::synchronize()
{
  while (m_free_threads != m_thread_mask)
  {
    wait();
  }
}

Dispatcher *g_dispatcher;

// Sequence
struct Sequence
{
    unsigned len = 0;
    uint8_t seq[24];

    bool operator==(const Sequence &s) const;
    bool operator!=(const Sequence &s) const;
    bool operator<(const Sequence &s) const;
    bool operator>(const Sequence &s) const;
    bool operator<=(const Sequence &s) const;
    bool operator>=(const Sequence &s) const;
    bool contains(const Sequence &s) const;
    Sequence operator+(const Sequence &s) const;
    void print() const;
};

bool Sequence::operator==(const Sequence &s) const
{
    if (len != s.len)
        return false;

    for (unsigned i = 0; i < len; ++i)
    {
        if (seq[i] != s.seq[i])
            return false;
    }

    return true;
}

bool Sequence::operator!=(const Sequence &s) const
{
    if (len != s.len)
        return true;

    for (unsigned i = 0; i < len; ++i)
    {
        if (seq[i] != s.seq[i])
            return true;
    }

    return false;
}

bool Sequence::operator<(const Sequence &s) const
{
    unsigned bound = std::min(len, s.len);
    for (unsigned i = 0; i < bound; ++i)
    {
        if (seq[i] < s.seq[i])
            return true;
        else if (seq[i] > s.seq[i])
            return false;
    }

    return (len < s.len);
}

bool Sequence::operator>(const Sequence &s) const
{
    unsigned bound = std::min(len, s.len);
    for (unsigned i = 0; i < bound; ++i)
    {
        if (seq[i] > s.seq[i])
            return true;
        else if (seq[i] < s.seq[i])
            return false;
    }

    return (len > s.len);
}

bool Sequence::operator<=(const Sequence &s) const
{
    unsigned bound = std::min(len, s.len);
    for (unsigned i = 0; i < bound; ++i)
    {
        if (seq[i] < s.seq[i])
            return true;
        else if (seq[i] > s.seq[i])
            return false;
    }

    return (len <= s.len);
}

bool Sequence::operator>=(const Sequence &s) const
{
    unsigned bound = std::min(len, s.len);
    for (unsigned i = 0; i < bound; ++i)
    {
        if (seq[i] > s.seq[i])
            return true;
        else if (seq[i] < s.seq[i])
            return false;
    }

    return (len >= s.len);
}

bool Sequence::contains(const Sequence &s) const
{
    for (unsigned i = 0, j = 0; i < s.len; ++i, ++j)
    {
        uint8_t el = s.seq[i];
        for (;; ++j)
        {
            if (j == len || seq[j] > el)
                return false;
            else if (seq[j] == el)
                break;
        }
    }

    return true;
}

Sequence Sequence::operator+(const Sequence &s) const
{
    if (!len)
        return s;
    else if (!s.len)
        return *this;

    Sequence r;
    r.len = len + s.len;
    unsigned pos = 0, i = 0, j = 0;
    while (true)
    {
        if (seq[i] <= s.seq[j])
        {
            r.seq[pos++] = seq[i++];
            if (i == len)
            {
                while (j < s.len)
                    r.seq[pos++] = s.seq[j++];
                break;
            }
        }
        else
        {
            r.seq[pos++] = s.seq[j++];
            if (j == s.len)
            {
                while (i < len)
                    r.seq[pos++] = seq[i++];
                break;
            }
        }
    }

    return r;
}

void Sequence::print() const
{
    for (unsigned i = 0; i < len; ++i)
        printf("%u ", seq[i]);
    printf("\n");
    fflush(stdout);
}

#define TRIE_SIZE 200

struct SeqTrieNode
{
    SeqTrieNode *links[TRIE_SIZE] = {};

    ~SeqTrieNode()
    {
        for (int i = 0; i < TRIE_SIZE; ++i)
            delete links[i];
    }

    bool contains(const Sequence &s, unsigned pos) const
    {
        if (pos == s.len)
            return true;
        
        uint8_t v = s.seq[pos];
        if (links[v] && links[v]->contains(s, pos + 1))
            return true;

        for (unsigned i = 0; i < v; ++i)
        {
            if (links[i] && links[i]->contains(s, pos))
                return true;
        }

        return false;
    }
};

struct SeqTrie
{
    SeqTrieNode root;

    void add(const Sequence &s)
    {
        SeqTrieNode *node = &root;
        for (unsigned pos = 0; pos < s.len; ++pos)
        {
            uint8_t v = s.seq[pos];
            if (!node->links[v])
                node->links[v] = new SeqTrieNode();
            node = node->links[v];
        }
    }

    void add(const std::set<Sequence> &s)
    {
        for (auto it = s.begin(); it != s.end(); ++it)
            add(*it);
    }

    bool contains(const Sequence &s) const
    {
        return root.contains(s, 0);
    }
};


void generate1(
    std::set<Sequence> &set, Sequence &pref, unsigned len, 
    unsigned sum, unsigned bmin, unsigned bmax
)
{
    if (len == 0)
    {
        if (sum == 0)
            set.insert(pref);
        return;
    }

    for (unsigned i = bmin; i <= bmax && i <= sum; ++i)
    {
        Sequence copy = pref;
        copy.seq[copy.len] = i;
        ++copy.len;
        generate1(set, copy, len - 1, sum - i, i, bmax);
    }
}

void generate2(
    std::set<Sequence> &set, Sequence &pref, unsigned len, 
    unsigned sum, unsigned bmin, unsigned bmod
)
{
    if (len == 0)
    {
        if (sum == 0)
            set.insert(pref);
        return;
    }

    for (unsigned i = bmin; i < bmod; ++i)
    {
        Sequence copy = pref;
        copy.seq[copy.len] = i;
        ++copy.len;
        unsigned newsum = (sum + bmod - i) % bmod;
        generate2(set, copy, len - 1, newsum, i, bmod);
    }
}

void merge1(
    std::set<Sequence> &set1, std::set<Sequence> &set2, 
    std::set<Sequence> &rset
)
{
    for (auto it = set1.begin(); it != set1.end(); ++it)
    {
        for (auto jt = set2.begin(); jt != set2.end(); ++jt)
        {
            Sequence r = *it + *jt;
            rset.insert(r);
        }
    }
}

struct MergeData
{
  std::set<Sequence>::iterator start1;
  std::set<Sequence>::iterator end1;
  std::set<Sequence>::iterator start2;
  std::set<Sequence>::iterator end2;
  std::set<Sequence> rset;
  std::set<Sequence> *grset;
  std::set<Sequence> *lowset;
  const SeqTrie *ref;
  unsigned bmod;
};

class MergeTask2 : public Task
{
public:
  MergeData dd;

  void run()
  {
    for (auto it = dd.start1; it != dd.end1; ++it)
    {
        for (auto jt = dd.start2; jt != dd.end2; ++jt)
        {
            Sequence r = *it + *jt;
            if (dd.rset.find(r) != dd.rset.end())
                continue;
            Sequence rcopy;
            rcopy.len = r.len;
            for (unsigned i = 0; i < r.len; ++i)
                rcopy.seq[i] = r.seq[i] % dd.bmod;
            std::sort(&rcopy.seq[0], &rcopy.seq[rcopy.len]);
            if (dd.ref->contains(rcopy))
                dd.rset.insert(r);
        }
    }
  }

  void finalize()
  {
    for (auto it = dd.rset.begin(); it != dd.rset.end(); ++it)
      dd.grset->insert(*it);
  }
};

#define TASK_COUNT 20

void merge2(
    std::set<Sequence> &set1, std::set<Sequence> &set2, 
    std::set<Sequence> &rset, const SeqTrie &ref, unsigned bmod
)
{
  auto start1 = set1.begin();
  long count = 0;
  for (auto it = set1.begin(); ; ++it, ++count)
  {
    if (count == TASK_COUNT || it == set1.end())
    {
      MergeTask2 *task = new MergeTask2();
      task->dd.start1 = start1;
      task->dd.end1 = it;
      task->dd.start2 = set2.begin();
      task->dd.end2 = set2.end();
      task->dd.grset = &rset;
      task->dd.ref = &ref;
      task->dd.bmod = bmod;
      g_dispatcher->schedule(task);

      start1 = it;
      count = 0;
    }

    if (it == set1.end())
      break;
  }
  g_dispatcher->synchronize();
}

class MergeTask4 : public Task
{
public:
  MergeData dd;

  void run()
  {
    for (auto it = dd.start1; it != dd.end1; ++it)
    {
        for (auto jt = dd.start2; jt != dd.end2; ++jt)
        {
            Sequence r = *it + *jt;
            if (dd.rset.find(r) != dd.rset.end())
                continue;
            if (dd.ref->contains(r))
                dd.rset.insert(r);
        }
    }
  }

  void finalize()
  {
    for (auto it = dd.rset.begin(); it != dd.rset.end(); ++it)
      dd.grset->insert(*it);
  }
};

void merge4(
    std::set<Sequence> &set1, std::set<Sequence> &set2, 
    std::set<Sequence> &rset, const SeqTrie &ref
)
{
  auto start1 = set1.begin();
  long count = 0;
  for (auto it = set1.begin(); ; ++it, ++count)
  {
    if (count == TASK_COUNT || it == set1.end())
    {
      MergeTask4 *task = new MergeTask4();
      task->dd.start1 = start1;
      task->dd.end1 = it;
      task->dd.start2 = set2.begin();
      task->dd.end2 = set2.end();
      task->dd.grset = &rset;
      task->dd.ref = &ref;
      g_dispatcher->schedule(task);

      start1 = it;
      count = 0;
    }

    if (it == set1.end())
      break;
  }
  g_dispatcher->synchronize();
}

class MergeTask3 : public Task
{
public:
  MergeData dd;

  void run()
  {
    for (auto it = dd.start1; it != dd.end1; ++it)
    {
        for (auto jt = dd.start2; jt != dd.end2; ++jt)
        {
            Sequence r = *it + *jt;
            if (dd.lowset->find(r) == dd.lowset->end())
                continue;
            dd.rset.insert(r);
        }
    }
  }

  void finalize()
  {
    for (auto it = dd.rset.begin(); it != dd.rset.end(); ++it)
      dd.grset->insert(*it);
  }
};

void merge3(
    std::set<Sequence> &set1, std::set<Sequence> &set2, 
    std::set<Sequence> &rset, std::set<Sequence> &lowset
)
{
  auto start1 = set1.begin();
  long count = 0;
  for (auto it = set1.begin(); ; ++it, ++count)
  {
    if (count == TASK_COUNT || it == set1.end())
    {
      MergeTask3 *task = new MergeTask3();
      task->dd.start1 = start1;
      task->dd.end1 = it;
      task->dd.start2 = set2.begin();
      task->dd.end2 = set2.end();
      task->dd.grset = &rset;
      task->dd.lowset = &lowset;
      g_dispatcher->schedule(task);

      start1 = it;
      count = 0;
    }

    if (it == set1.end())
      break;
  }
  g_dispatcher->synchronize();
}

void trim1(
    std::set<Sequence> &set, const std::set<Sequence> &ref, unsigned bmod
)
{
    for (auto it = set.begin(); it != set.end();)
    {
        auto next = it;
        ++next;

        Sequence s = *it;
        for (unsigned j = 0; j < s.len; ++j)
            s.seq[j] = s.seq[j] % bmod;
        std::sort(&s.seq[0], &s.seq[s.len]);

        bool found = false;
        for (auto jt = ref.begin(); jt != ref.end(); ++jt)
        {
            if (jt->contains(s))
            {
                found = true;
                break;
            }
        }
        if (!found)
            set.erase(it);
        it = next;
    }
}

void trim2(
    std::set<Sequence> &set, const SeqTrie &ref, unsigned bmod
)
{
    for (auto it = set.begin(); it != set.end();)
    {
        auto next = it;
        ++next;

        Sequence s = *it;
        for (unsigned j = 0; j < s.len; ++j)
            s.seq[j] = s.seq[j] % bmod;
        std::sort(&s.seq[0], &s.seq[s.len]);

        if (!ref.contains(s))
            set.erase(it);
        it = next;
    }
}

void trim3(
    std::set<Sequence> &set, const SeqTrie &ref
)
{
    for (auto it = set.begin(); it != set.end();)
    {
        auto next = it;
        ++next;

        if (!ref.contains(*it))
            set.erase(it);
        it = next;
    }
}

void generate3(
    std::set<Sequence> &set, unsigned sum, unsigned mrob, 
    unsigned stones, unsigned bmod
)
{
    Sequence pref;
    std::set<Sequence> vars[3];
    std::set<Sequence> locs[3];
    generate1(vars[0], pref, mrob, stones, 2, stones);
    generate1(vars[1], pref, mrob + 1, stones, 2, stones);
    generate1(vars[2], pref, mrob + 2, stones, 1, stones);

    for (int i = 0; i < 3; ++i)
    {
        unsigned modsum = (sum / (mrob + i)) % bmod;
        printf("vars[i] = %lu\n", vars[i].size());
        fflush(stdout);
        for (auto it = vars[i].begin(); it != vars[i].end(); ++it)
        {
            std::set<Sequence> end;
            for (unsigned j = 0; j < it->len; ++j)
            {
                std::set<Sequence> low, mid;
                generate2(low, pref, it->seq[j], modsum, 0, bmod);
                if (j == 0)
                {
                    end.swap(low);
                }
                else
                {
                    merge1(end, low, mid);
                    end.swap(mid);
                }
            }

            for (auto jt = end.begin(); jt != end.end(); ++jt)
            {
                locs[i].insert(*jt);
            }
        }
    }

    printf("locs[0].size = %lu\n", locs[0].size());
    printf("locs[1].size = %lu\n", locs[1].size());
    printf("locs[2].size = %lu\n", locs[2].size());
    fflush(stdout);
    for (auto it = locs[0].begin(); it != locs[0].end(); ++it)
    {
        if (locs[1].find(*it) == locs[1].end())
            continue;
        if (locs[2].find(*it) == locs[2].end())
            continue;
        set.insert(*it);
    }

    printf("set.size = %lu\n", set.size());
    fflush(stdout);
}

void generate4(
    std::set<Sequence> &set, const SeqTrie &ref,
    unsigned sum, unsigned mrob, 
    unsigned stones, unsigned bmod
)
{
    Sequence pref;
    std::set<Sequence> vars[3];
    std::set<Sequence> locs[3];
    generate1(vars[0], pref, mrob, stones, 2, stones);
    generate1(vars[1], pref, mrob + 1, stones, 2, stones);
    generate1(vars[2], pref, mrob + 2, stones, 1, stones);
    unsigned maxsum = sum / (mrob + 2);

    for (int i = 2; i >= 1; --i)
    {
        unsigned modsum = (sum / (mrob + i));
        printf("VARS[%d] = %lu\n", i, vars[i].size());
        fflush(stdout);
        unsigned step = 1;
        for (auto it = vars[i].begin(); it != vars[i].end(); ++it, ++step)
        {
            std::set<Sequence> end;
            bool stop = false;
            for (unsigned j = 0; j < it->len; ++j)
            {
                std::set<Sequence> low, mid;
                generate1(low, pref, it->seq[j], modsum, 1, maxsum);
                trim2(low, ref, bmod);
                if (low.empty())
                {
                    stop = true;
                    break;
                }
                if (j == 0)
                {
                    end.swap(low);
                }
                else
                {
                    if (i == 2 || j + 1 < it->len)
                        merge2(end, low, mid, ref, bmod);
                    else
                        merge3(end, low, mid, locs[i+1]);
                    if (mid.empty())
                    {
                        stop = true;
                        break;
                    }
                    end.swap(mid);
                }
            }

            printf("STEP = %u, ENDS = %lu, STOP=%d\n", step, end.size(), stop);
            fflush(stdout);
            if (stop)
                continue;

            for (auto jt = end.begin(); jt != end.end(); ++jt)
            {
                locs[i].insert(*jt);
            }
        }

        printf("locs[%d].size = %lu\n", i, locs[i].size());
        fflush(stdout);
        if (locs[i].size() == 0)
            return;
    }

    //SeqTrie myref;
    //myref.add(locs[1]);
    size_t count = 0;
    for (auto kt = locs[1].begin(); kt != locs[1].end(); ++kt, ++count)
    {
      if ((count % 1000) == 0)
      {
        printf("count = %lu\n", count);
        fflush(stdout);
      }
      SeqTrie myref;
      std::set<Sequence> mylocs;
      mylocs.insert(*kt);
      myref.add(*kt);
      for (int i = 0; i == 0; --i)
      {
          unsigned modsum = (sum / (mrob + i));
          unsigned step = 1;
          for (auto it = vars[i].begin(); it != vars[i].end(); ++it, ++step)
          {
              std::set<Sequence> end;
              bool stop = false;
              for (unsigned j = 0; j < it->len; ++j)
              {
                  std::set<Sequence> low, mid;
                  generate1(low, pref, it->seq[j], modsum, 1, maxsum);
                  trim3(low, myref);
                  if (low.empty())
                  {
                      stop = true;
                      break;
                  }
                  if (j == 0)
                  {
                      end.swap(low);
                  }
                  else
                  {
                      if (i == 2 || j + 1 < it->len)
                          merge4(end, low, mid, myref);
                      else
                          merge3(end, low, mid, mylocs);
                      if (mid.empty())
                      {
                          stop = true;
                          break;
                      }
                      end.swap(mid);
                  }
              }

              if (stop)
                  continue;

              for (auto jt = end.begin(); jt != end.end(); ++jt)
              {
                  locs[i].insert(*jt);
              }
          }
      }
    }

    printf("locs[%d].size = %lu\n", 0, locs[0].size());
    fflush(stdout);
    set.swap(locs[0]);
    printf("set.size = %lu\n", set.size());
    fflush(stdout);
}

void generate_bs(
    std::set<Sequence> &set, const SeqTrie &ref,
    unsigned sum, unsigned mrob, 
    unsigned stones, unsigned bmod
)
{
    Sequence pref;
    std::set<Sequence> vars[3];
    std::set<Sequence> locs[3];
    generate1(vars[0], pref, mrob, stones, 2, stones);
    generate1(vars[1], pref, mrob + 1, stones, 2, stones);
    generate1(vars[2], pref, mrob + 2, stones, 1, stones);

    for (int i = 2; i >= 1; --i)
    {
        unsigned modsum = (sum / (mrob + i)) % (2 * bmod);
        printf("VARS[%d] = %lu\n", i, vars[i].size());
        fflush(stdout);
        unsigned step = 1;
        for (auto it = vars[i].begin(); it != vars[i].end(); ++it, ++step)
        {
            std::set<Sequence> end;
            bool stop = false;
            for (unsigned j = 0; j < it->len; ++j)
            {
                std::set<Sequence> low, mid;
                generate2(low, pref, it->seq[j], modsum, 0, 2 * bmod);
                trim2(low, ref, bmod);
                if (low.empty())
                {
                    stop = true;
                    break;
                }
                if (j == 0)
                {
                    end.swap(low);
                }
                else
                {
                    if (i == 2 || j + 1 < it->len)
                        merge2(end, low, mid, ref, bmod);
                    else
                        merge3(end, low, mid, locs[i+1]);
                    if (mid.empty())
                    {
                        stop = true;
                        break;
                    }
                    end.swap(mid);
                }
            }

            printf("STEP = %u, ENDS = %lu, STOP=%d\n", step, end.size(), stop);
            fflush(stdout);
            if (stop)
                continue;

            for (auto jt = end.begin(); jt != end.end(); ++jt)
            {
                locs[i].insert(*jt);
            }
        }

        printf("locs[%d].size = %lu\n", i, locs[i].size());
        fflush(stdout);
        if (locs[i].size() == 0)
            return;
    }

    //SeqTrie myref;
    //myref.add(locs[1]);
    size_t count = 0;
    for (auto kt = locs[1].begin(); kt != locs[1].end(); ++kt, ++count)
    {
      if ((count % 1000) == 0)
      {
        printf("count = %lu\n", count);
        fflush(stdout);
      }
      SeqTrie myref;
      std::set<Sequence> mylocs;
      mylocs.insert(*kt);
      myref.add(*kt);
      for (int i = 0; i == 0; --i)
      {
          unsigned modsum = (sum / (mrob + i)) % (2 * bmod);
          unsigned step = 1;
          for (auto it = vars[i].begin(); it != vars[i].end(); ++it, ++step)
          {
              std::set<Sequence> end;
              bool stop = false;
              for (unsigned j = 0; j < it->len; ++j)
              {
                  std::set<Sequence> low, mid;
                  generate2(low, pref, it->seq[j], modsum, 0, 2*bmod);
                  trim3(low, myref);
                  //trim2(low, ref, bmod);
                  if (low.empty())
                  {
                      stop = true;
                      break;
                  }
                  if (j == 0)
                  {
                      end.swap(low);
                  }
                  else
                  {
                      if (i == 2 || j + 1 < it->len)
                          merge4(end, low, mid, myref);
                          //merge2(end, low, mid, ref, bmod);
                      else
                          merge3(end, low, mid, mylocs);
                      if (mid.empty())
                      {
                          stop = true;
                          break;
                      }
                      end.swap(mid);
                  }
              }

              if (stop)
                  continue;

              for (auto jt = end.begin(); jt != end.end(); ++jt)
              {
                  locs[i].insert(*jt);
              }
          }
      }
    }

    printf("locs[%d].size = %lu\n", 0, locs[0].size());
    fflush(stdout);

    set.swap(locs[0]);
    printf("set.size = %lu\n", set.size());
    fflush(stdout);
}

// main
int main()
{
    Dispatcher disp(8);
    g_dispatcher = &disp;

    Sequence pref;
    std::set<Sequence> ref1, fnd1, fnd2, fnd3;
    SeqTrie tref1, tref2, tref3;

    printf("GEN1\n");
    fflush(stdout);
    generate3(ref1, 7*8*9, 7, 18, 7);
    tref1.add(ref1);
    printf("GEN2\n");
    fflush(stdout);
    generate_bs(fnd1, tref1, 7*8*9, 7, 18, 7);
    tref2.add(fnd1);
    printf("GEN3\n");
    fflush(stdout);
    generate_bs(fnd2, tref2, 7*8*9, 7, 18, 7 * 2);
    printf("GEN4\n");
    fflush(stdout);
    tref3.add(fnd2);
    generate4(fnd3, tref3, 7*8*9, 7, 18, 7 * 4);

    for (auto it = fnd3.begin(); it != fnd3.end(); ++it)
    {
        it->print();
    }
}


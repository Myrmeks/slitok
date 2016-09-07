#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <vector>
#include <set>
#include <algorithm>

#define ROBBERS_MIN 7
#define STONES_NUM 18
#define ROBSUM0 (ROBBERS_MIN * (ROBBERS_MIN + 1))
#define ROBSUM1 (ROBBERS_MIN * (ROBBERS_MIN + 2))
#define ROBSUM2 ((ROBBERS_MIN + 1) * (ROBBERS_MIN + 2))

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

struct Ratnum
{
	int64_t n, d;

	Ratnum() : n(0), d(1) {}
	Ratnum(int64_t _v) : n(_v), d(1) {}
	Ratnum(int64_t _n, int64_t _d) : n(_n), d(_d) {}

	void print() const
	{
		if (d == 1)
			printf("%ld", n);
		else
			printf("%ld/%ld", n, d);
	}
};

inline int64_t gcd(int64_t a, int64_t b)
{
	if (a == 0)
		return b;
	else if (b == 0)
		return a;

	if (a < 0)
		a = -a;

	if (b < 0)
		b = -b;	

	if (a < b)
	{
		int64_t tmp = a;
		a = b;
		b = tmp;
	}

	if (b == 1)
		return 1;
	else if (b == 2)
		return ((a % 2) == 0) ? 2 : 1;

	while (true)
	{
		int64_t r = a % b;
		if (r == 0)
			return b;
		a = b;
		b = r;
	}
}

inline bool operator<(Ratnum r1, Ratnum r2)
{
	return (r1.n * r2.d < r2.n * r1.d);
}

inline Ratnum operator+(Ratnum r1, Ratnum r2)
{
	if (r1.n == 0)
		return r2;
	else if (r2.n == 0)
		return r1;
	int64_t n = r1.n * r2.d + r2.n * r1.d;
	if (n == 0)
		return Ratnum{0, 1};
	int64_t d = r1.d * r2.d;
	int64_t g = gcd(n, d);
	return Ratnum{n/g, d/g};
}

inline Ratnum operator-(Ratnum r1, Ratnum r2)
{
	if (r1.n == 0)
		return Ratnum{-r2.n, r2.d};
	else if (r2.n == 0)
		return r1;
	int64_t n = r1.n * r2.d - r2.n * r1.d;
	if (n == 0)
		return Ratnum{0, 1};
	int64_t d = r1.d * r2.d;
	int64_t g = gcd(n, d);
	return Ratnum{n/g, d/g};
}

inline Ratnum operator*(Ratnum r1, Ratnum r2)
{
	if (r1.n == 0 || r2.n == 0)
		return Ratnum{0, 1};
	int64_t n = r1.n * r2.n;
	int64_t d = r1.d * r2.d;
	int64_t g = gcd(n, d);
	return Ratnum{n/g, d/g};
}

inline Ratnum operator/(Ratnum r1, Ratnum r2)
{
	if (r1.n == 0)
		return Ratnum{0, 1};
	int64_t n = r1.n * r2.d;
	int64_t d = r1.d * r2.n;
	if (d < 0)
	{
		d = -d;
		n = -n;
	}
	int64_t g = gcd(n, d);
	return Ratnum{n/g, d/g};
}

inline Ratnum rn_submul(Ratnum a, Ratnum b, Ratnum c)
{
	int64_t bb = b.d * c.d;
	int64_t n = a.n * bb - a.d * b.n * c.n;
	if (n == 0)
		return Ratnum {0, 1};
	int64_t d = a.d * bb;
	int64_t g = gcd(n, d);
	return Ratnum{n/g, d/g};
}

struct Sequence
{
	int len;
	uint8_t seq[20];
};

struct IntSeq
{
	int len;
	int64_t seq[20];

	bool operator<(const IntSeq &s) const
	{
		int bound = std::min(len, s.len);
    for (int i = 0; i < bound; ++i)
    {
        if (seq[i] < s.seq[i])
            return true;
        else if (s.seq[i] < seq[i])
            return false;
    }

    return (len < s.len);
	}
};

struct RatSeq
{
	int len;
	Ratnum seq[20];

	bool operator<(const RatSeq &s) const
	{
		int bound = std::min(len, s.len);
    for (int i = 0; i < bound; ++i)
    {
        if (seq[i] < s.seq[i])
            return true;
        else if (s.seq[i] < seq[i])
            return false;
    }

    return (len < s.len);
	}

	void print() const
	{
		for (int i = 0; i < len; ++i)
		{
			seq[i].print();
			printf(" ");
		}
		printf("\n");
	}
};

void gen_boxes_impl(
	std::vector<Sequence> &vec, int bnum, int min, int sum,
	Sequence &seq, int spos
)
{
	if (bnum == 0)
	{
		if (sum == 0)
			vec.push_back(seq);
		return;
	}

	for (int i = min; i <= sum; ++i)
	{
		seq.seq[spos] = i;
		gen_boxes_impl(vec, bnum - 1, i, sum - i, seq, spos + 1);
	}
}

void gen_boxes(std::vector<Sequence> &vec, int bnum, int min, int sum)
{
	Sequence seqtmp;
	seqtmp.len = bnum;
	gen_boxes_impl(vec, bnum, min, sum, seqtmp, 0);
}

struct MiddleIter
{
	int index[10];
	int len;
	int bound;
	bool is_valid;

	MiddleIter(int _len, int _bound)
		: len(_len), bound(_bound), is_valid(false)
	{
	}

	void first()
	{
		is_valid = true;
		for (int i = 0; i < len; ++i)
			index[i] = 0;
	}

	void set(uint8_t *p)
	{
		is_valid = true;
		for (int i = 0; i < len; ++i)
			index[i] = p[i];
	}

	bool valid()
	{
		return is_valid;
	}

	void next()
	{
		for (int i = len - 1; i >= 0; --i)
		{
			++index[i];
			if (index[i] < bound)
			{
				for (int j = i + 1; j < len; ++j)
					index[j] = index[i];
				return;
			}
		}
		is_valid = false;
	}

	bool unique()
	{
		for (int i = 0; i + 1 < len; ++i)
		{
			if (index[i] == index[i + 1])
				return false;
		}

		return true;
	}
};

#define TRIE_SIZE 5
struct SeqTrieNode
{
    SeqTrieNode *links[TRIE_SIZE] = {};

    ~SeqTrieNode()
    {
        for (int i = 0; i < TRIE_SIZE; ++i)
            delete links[i];
    }
};

struct SeqTrie
{
    SeqTrieNode root;

    void add(const Sequence &s)
    {
        SeqTrieNode *node = &root;
        for (int pos = 0; pos < s.len; ++pos)
        {
            uint8_t v = s.seq[pos];
            if (!node->links[v])
                node->links[v] = new SeqTrieNode();
            node = node->links[v];
        }
    }
};

void gen_middle_impl(
	std::vector<Sequence> &vec, Sequence &boxes0, Sequence &boxes1,
	Sequence &seq, int spos, int bpos, Sequence &orboxes0
)
{
	if (bpos == boxes1.len)
	{
		seq.len = spos;
		vec.push_back(seq);
		return;
	}

	MiddleIter it(boxes1.seq[bpos], boxes0.len);
	if (bpos > 0 && it.len == boxes1.seq[bpos - 1])
		it.set(seq.seq + spos - it.len);
	else
		it.first();

	for (; it.valid(); it.next())
	{
		int lspos = spos;
		Sequence bcopy0 = boxes0;
		bool skip = false;
		for (int i = 0; i < it.len; ++i, ++lspos)
		{
			int index = it.index[i];
			if (bcopy0.seq[index] == 0)
			{
				skip = true;
				break;
			}

			if (index > 0 && orboxes0.seq[index] == orboxes0.seq[index - 1])
			{
				bool found = false;
				for (int j = 0; j < lspos; ++j)
				{
					if (seq.seq[j] == index - 1)
					{
						found = true;
						break;
					}
				}

				if (!found)
				{
					skip = true;
					break;
				}
			}

			--bcopy0.seq[index];
			seq.seq[lspos] = index;
		}

		if (skip)
			continue;

		gen_middle_impl(vec, bcopy0, boxes1, seq, lspos, bpos + 1, orboxes0);
	}
}

void gen_middle(
	std::vector<Sequence> &vec, Sequence &boxes0, Sequence &boxes1
)
{
	Sequence seq;
	gen_middle_impl(vec, boxes0, boxes1, seq, 0, 0, boxes0);
}

struct Matrix
{
	Ratnum mat[24][20] = {};
	int mv[24] = {};
	int fv[20] = {};
	int fvlen = 0;
};

int make_matrix(
	Matrix &m, Sequence &boxes0, Sequence &boxes1, Sequence &middle
)
{
	int poses[9];
	int nf[20] = {};
	for (int i = 0, s = 0; i < boxes0.len; ++i)
	{
		poses[i] = s;
		m.mv[i] = s;
		nf[s] = 1;
		for (int j = 0; j < boxes0.seq[i]; ++j, ++s)
			m.mat[i][s] = Ratnum{1};
		m.mat[i][STONES_NUM] = Ratnum{ROBSUM0};
	}

	for (int i = 0, s = 0; i < boxes1.len; ++i)
	{
		for (int j = 0; j < boxes1.seq[i]; ++j, ++s)
		{
			m.mat[i + boxes0.len][poses[middle.seq[s]]] = Ratnum{1};
			++poses[middle.seq[s]];
		}
		m.mat[i + boxes0.len][STONES_NUM] = Ratnum{ROBSUM1};
	}

	int mh = boxes0.len + boxes1.len;
	int rang = mh;
	for (int i = 0; i < boxes0.len; ++i)
	{
		for (int j = boxes0.len; j < mh; ++j)
		{
			Ratnum alfa;
			if ((alfa = m.mat[j][m.mv[i]]).n == 0)
				continue;
			m.mat[j][m.mv[i]] = Ratnum{0};
			for (int k = m.mv[i] + 1; k <= STONES_NUM; ++k)
			{
				if (m.mat[i][k].n)
					m.mat[j][k] = m.mat[j][k] - m.mat[i][k] * alfa;
			}
		}
	}

	for (int i = boxes0.len; i < mh; ++i)
	{
		int vj = 0;
		for (; vj < STONES_NUM; ++vj)
		{
			if (m.mat[i][vj].n)
				break;
		}

		if (vj == STONES_NUM)
		{
			if (m.mat[i][STONES_NUM].n)
				return 0;
			m.mv[i] = -1;
			--rang;
			continue;
		}

		m.mv[i] = vj;
		nf[vj] = 1;
		if (m.mat[i][vj].n != 1 || m.mat[i][vj].d != 1)
		{
			Ratnum alfa = m.mat[i][vj];
			m.mat[i][vj] = Ratnum{1};
			for (int j = vj + 1; j <= STONES_NUM; ++j)
			{
				if (m.mat[i][j].n)
					m.mat[i][j] = m.mat[i][j] / alfa;
			}
		}

		for (int j = i + 1; j < mh; ++j)
		{
			Ratnum alfa;
			if ((alfa = m.mat[j][m.mv[i]]).n == 0)
				continue;
			m.mat[j][m.mv[i]] = Ratnum{0};
			for (int k = m.mv[i] + 1; k <= STONES_NUM; ++k)
			{
				if (m.mat[i][k].n)
					m.mat[j][k] = m.mat[j][k] - m.mat[i][k] * alfa;
			}
		}
	}

	for (int i = mh - 1; i > 0; --i)
	{
		if (m.mv[i] < 0)
			continue;
		for (int j = 0; j < i; ++j)
		{
			if (m.mv[j] < 0)
				continue;
			Ratnum alfa;
			if ((alfa = m.mat[j][m.mv[i]]).n == 0)
				continue;
			m.mat[j][m.mv[i]] = Ratnum{0};
			for (int k = m.mv[i] + 1; k <= STONES_NUM; ++k)
			{
				if (m.mat[i][k].n)
					m.mat[j][k] = m.mat[j][k] - m.mat[i][k] * alfa;
			}
		}
	}

	for (int i = 0; i < STONES_NUM; ++i)
	{
		if (!nf[i])
		{
			m.fv[m.fvlen] = i;
			++m.fvlen;
		}
	}

	return rang;
}

bool precheck_matrix(Matrix &m)
{
	int mh = ROBBERS_MIN * 2 + 3;
	for (int i = 0; i < mh; ++i)
	{
		if (m.mv[i] < 0)
			continue;

		bool skip = false;
		for (int j = 0; j < m.fvlen; ++j)
		{
			if (m.mat[i][m.fv[j]].n < 0)
			{
				skip = true;
				break;
			}
		}

		if (skip)
			continue;
		
		if (m.mat[i][STONES_NUM].n <= 0)
			return false;
	}

	return true;
}

bool add_to_matrix(Matrix &m, int level, MiddleIter &mit)
{
	int mh = ROBBERS_MIN * 2 + 3;
	for (int i = 0; i < m.fvlen; ++i)
		m.mat[mh + level][m.fv[i]] = Ratnum{0};

	for (int i = 0; i < mit.len; ++i)
		m.mat[mh + level][mit.index[i]] = Ratnum{1};
	m.mat[mh + level][STONES_NUM] = Ratnum{ROBSUM2};

	for (int i = 0; i < mh; ++i)
	{
		if (m.mv[i] < 0)
			continue;

		int j = mh + level;
		Ratnum alfa;
		if ((alfa = m.mat[j][m.mv[i]]).n == 0)
			continue;
		m.mat[j][m.mv[i]] = Ratnum{0};
		for (int k = 0; k < m.fvlen; ++k)
		{
			int kk = m.fv[k];
			if (m.mat[i][kk].n)
				m.mat[j][kk] = rn_submul(m.mat[j][kk], m.mat[i][kk], alfa);
		}

		int kk = STONES_NUM;
		if (m.mat[i][kk].n)
			m.mat[j][kk] = rn_submul(m.mat[j][kk], m.mat[i][kk], alfa);
	}

	for (int i = mh; i < mh + level; ++i)
	{
		int j = mh + level;
		Ratnum alfa;
		if ((alfa = m.mat[j][m.mv[i]]).n == 0)
			continue;
		for (int k = 0; k < m.fvlen; ++k)
		{
			int kk = m.fv[k];
			if (m.mat[i][kk].n)
				m.mat[j][kk] = rn_submul(m.mat[j][kk], m.mat[i][kk], alfa);
		}

		int kk = STONES_NUM;
		if (m.mat[i][kk].n)
			m.mat[j][kk] = rn_submul(m.mat[j][kk], m.mat[i][kk], alfa);
	}

	int ll = mh + level;
	for (int i = 0; i < m.fvlen; ++i)
	{
		int vj = m.fv[i];
		if (m.mat[ll][vj].n)
		{
			m.mv[ll] = vj;
			if (m.mat[ll][vj].n != 1 || m.mat[ll][vj].d != 1)
			{
				Ratnum alfa = m.mat[ll][vj];
				m.mat[ll][vj] = Ratnum{1};
				for (int j = i + 1; j < m.fvlen; ++j)
				{
					int kk = m.fv[j];
					if (m.mat[ll][kk].n)
						m.mat[ll][kk] = m.mat[ll][kk] / alfa;
				}

				int kk = STONES_NUM;
				if (m.mat[ll][kk].n)
					m.mat[ll][kk] = m.mat[ll][kk] / alfa;
			}
			return true;
		}
	}

	return false;
}

bool solve_matrix(Matrix &m, int level, Ratnum *sol)
{
	int mh = ROBBERS_MIN * 2 + 3;
	for (int i = level - 1; i >= 0; --i)
	{
		Ratnum s = m.mat[mh + i][STONES_NUM];
		for (int j = 0; j < m.fvlen; ++j)
		{
			if (m.fv[j] == m.mv[mh + i])
				continue;
			if (m.mat[mh + i][m.fv[j]].n == 0)
				continue;
			s = rn_submul(s, sol[m.fv[j]], m.mat[mh + i][m.fv[j]]);
		}
		if (s.n <= 0)
			return false;
		sol[m.mv[mh + i]] = s;
	}

	for (int i = 0; i < mh; ++i)
	{
		if (m.mv[i] < 0)
			continue;
		Ratnum s = m.mat[i][STONES_NUM];
		for (int j = 0; j < m.fvlen; ++j)
		{
			if (m.mat[i][m.fv[j]].n == 0)
				continue;
			s = rn_submul(s, sol[m.fv[j]], m.mat[i][m.fv[j]]);
		}
		if (s.n <= 0)
			return false;
		sol[m.mv[i]] = s;
	}

	return true;
}

int64_t lcm(int64_t a, int64_t b)
{
	if (a == 1)
		return b;
	else if (b == 1)
		return a;
	return a * (b / gcd(a, b));
}

int64_t integrate_seq(RatSeq &rseq, IntSeq &iseq)
{
	iseq.len = rseq.len;
	int64_t maxd = 1;
	for (int i = 0; i < rseq.len; ++i)
	{
		if (rseq.seq[i].d > maxd)
			maxd = rseq.seq[i].d;
	}

	if (maxd == 1)
	{
		for (int i = 0; i < rseq.len; ++i)
			iseq.seq[i] = rseq.seq[i].n;
		return 1;
	}
	else if (maxd == 2)
	{
		for (int i = 0; i < rseq.len; ++i)
		{
			if (rseq.seq[i].d == 2)
				iseq.seq[i] = rseq.seq[i].n;
			else
				iseq.seq[i] = rseq.seq[i].n * 2;
		}
		return 2;
	}

	int g = 1;
	for (int i = 0; i < rseq.len; ++i)
		g = lcm(g, rseq.seq[i].d);

	for (int i = 0; i < rseq.len; ++i)
		iseq.seq[i] = rseq.seq[i].n * (g / rseq.seq[i].d);

	return g;
}

bool check_stones(IntSeq &stones, int spos, int bnum, int64_t bw, int64_t mbw)
{
	if (bnum <= 1)
		return true;

	if (bw == mbw)
	{
		int i = stones.len - 1;
		for (; i >= 0 && !stones.seq[i]; --i) {}
		if (i < 0 || bw < stones.seq[i])
			return false;

		int64_t lbw = bw - stones.seq[i];
		int64_t olds = stones.seq[i];
		stones.seq[i] = 0;
		int lbnum;
		if (lbw > 0)
			lbnum = bnum;
		else
		{
			lbnum = bnum - 1;
			lbw = mbw;
		}

		bool rc = check_stones(stones, 0, lbnum, lbw, mbw);
		stones.seq[i] = olds;
		return rc;
	}

	for (int i = spos; i < stones.len; ++i)
	{
		if (stones.seq[i] == 0)
			continue;
		if (bw < stones.seq[i])
			break;
		int64_t lbw = bw - stones.seq[i];
		int64_t olds = stones.seq[i];
		stones.seq[i] = 0;
		int lbnum;
		int lspos;
		if (lbw > 0)
		{
			lbnum = bnum;
			lspos = i + 1;
		}
		else
		{
			lbnum = bnum - 1;
			lspos = 0;
			lbw = mbw;
		}

		bool rc = check_stones(stones, lspos, lbnum, lbw, mbw);
		stones.seq[i] = olds;
		if (rc)
			return true;
	}

	return false;
}

bool check_solution(IntSeq &seq, int bnum, int64_t mbw)
{
	int rc = check_stones(seq, 0, bnum, mbw, mbw);
	return rc;
}

void do_l3(
	std::set<RatSeq> &results, SeqTrieNode *tr, 
	Matrix &m, int level, int maxlevel, int used[], MiddleIter *prev
)
{
	if (level == maxlevel)
	{
		Ratnum sol[20];
		if (!solve_matrix(m, level, sol))
			return;
		RatSeq chseq, solseq;
		chseq.len = 0;
		solseq.len = 0;
		for (int i = 0; i < STONES_NUM; ++i)
		{
			solseq.seq[solseq.len] = sol[i];
			++solseq.len;
			if (!used[i])
			{
				chseq.seq[chseq.len] = sol[i];
				++chseq.len;
			}
		}

		IntSeq ichseq;
		int64_t g = integrate_seq(chseq, ichseq);
		std::sort(ichseq.seq, ichseq.seq + ichseq.len);
		if (!check_solution(ichseq, ROBBERS_MIN - level, ROBSUM2 * g))
			return;

		std::sort(solseq.seq, solseq.seq + solseq.len);
		results.insert(solseq);
		return;
	}

	for (int i = 0; i < TRIE_SIZE; ++i)
	{
		if (!tr->links[i])
			continue;

		MiddleIter it(i, STONES_NUM);
		for (it.first(); it.valid(); it.next())
		{
			if (!it.unique())
				continue;

			bool skip = false;
			if (prev && prev->len == i)
			{
				for (int j = 0; j < i; ++j)
				{
					if (it.index[j] > prev->index[j])
						break;
					else if (it.index[j] < prev->index[j])
					{
						skip = true;
						break;
					}
				}	
			}

			if (skip)
				continue;

			for (int j = 0; j < i; ++j)
			{
				if (used[it.index[j]])
				{
					skip = true;
					break;
				}
			}

			if (skip)
				continue;

			for (int j = 0; j < i; ++j)
				used[it.index[j]] = 1;

			if (add_to_matrix(m, level, it))
			{
				do_l3(results, tr->links[i], m, level + 1, maxlevel, used, &it);
			}

			for (int j = 0; j < i; ++j)
				used[it.index[j]] = 0;
		}
	}
}

void add_l3(SeqTrie &trie, Sequence &boxes2, int len)
{
	MiddleIter it(len, boxes2.len - 1);
	for (it.first(); it.valid(); it.next())
	{
		if (!it.unique())
			continue;
		Sequence seq;
		seq.len = len;
		for (int i = 0; i < len; ++i)
			seq.seq[i] = boxes2.seq[it.index[i]];
		trie.add(seq);
	}
}

class MyTask : public Task
{
public:
	Sequence *boxes0;
	Sequence *boxes1;
	std::vector<Sequence> *middle;
	size_t mstart;
	size_t mend;
	std::set<RatSeq> results;
	std::set<RatSeq> *global_results;
	SeqTrie *l3;
	long *count;
	long mycount;

	void run()
	{
		mycount = 0;
		for (size_t i = mstart; i < mend; ++i)
		{
			Matrix m;
			int rang = make_matrix(m, *boxes0, *boxes1, (*middle)[i]);
			if (!rang)
				continue;
			++mycount;
			if (!precheck_matrix(m))
				continue;
			int used[20] = {};
			do_l3(results, &l3[m.fvlen].root, m, 0, m.fvlen, used, 0);
		}
	}
	
	void finalize()
	{
		if (mycount)
		{
			*count += mycount;
			for (auto it = results.begin(); it != results.end(); ++it)
				global_results->insert(*it);
			fprintf(stderr, "count = %ld [%ld]\n", *count, global_results->size());
		}
	}
};

int main()
{
	Dispatcher dsp(8);
	std::set<RatSeq> results;
	std::vector<Sequence> boxes[3];
	gen_boxes(boxes[0], ROBBERS_MIN + 2, 1, STONES_NUM);
	gen_boxes(boxes[1], ROBBERS_MIN + 1, 2, STONES_NUM);
	gen_boxes(boxes[2], ROBBERS_MIN, 2, STONES_NUM);

	SeqTrie l3[10];
	for (auto it = boxes[2].begin(); it != boxes[2].end(); ++it)
	{
		for (int i = 1; i < ROBBERS_MIN; ++i)
			add_l3(l3[i], *it, i);
	}

	long count = 0;
	for (auto it = boxes[0].begin(); it != boxes[0].end(); ++it)
	{
		for (auto jt = boxes[1].begin(); jt != boxes[1].end(); ++jt)
		{
			std::vector<Sequence> middle;
			gen_middle(middle, *it, *jt);
			for (size_t k = 0; k < middle.size(); k += 20)
			{
				MyTask *task = new MyTask();
				task->boxes0 = &(*it);
				task->boxes1 = &(*jt);
				task->middle = &middle;
				task->mstart = k;
				task->mend = std::min(k + 20, middle.size());
				task->global_results = &results;
				task->l3 = l3;
				task->count = &count;
				dsp.schedule(task);
			}
			dsp.synchronize();
		}
	}

	printf("RESULTS = %lu\n", results.size());
	for (auto it = results.begin(); it != results.end(); ++it)
	{
		it->print();
	}
}



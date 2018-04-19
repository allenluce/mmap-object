/*
* Copyright (C) 2018 Christopher Gilbert.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

// Note: this is a heavily elided version of the original file, about
// 440 lines were removed. It's not full Aho Corasick but only detects
// whether a string is in a set of strings.

namespace aho_corasick {
	class state {
	public:
		typedef state*                           ptr;
		typedef std::unique_ptr<state>           unique_ptr;
		typedef std::basic_string<char>          string_type;
		typedef std::basic_string<char>&         string_ref_type;
		typedef std::pair<string_type, unsigned> key_index;
		typedef std::set<key_index>              string_collection;
		typedef std::vector<ptr>                 state_collection;
		typedef std::vector<char>                transition_collection;

	private:
		size_t                     d_depth;
		ptr                        d_root;
		std::map<char, unique_ptr> d_success;
		ptr                        d_failure;
		string_collection          d_emits;

	public:
		state(): state(0) {}

		explicit state(size_t depth)
			: d_depth(depth)
			, d_root(depth == 0 ? this : nullptr)
			, d_success()
			, d_failure(nullptr)
			, d_emits() {}

		ptr next_state(char character) const {
			return next_state(character, false);
		}

		ptr next_state_ignore_root_state(char character) const {
			return next_state(character, true);
		}

		ptr add_state(char character) {
			auto next = next_state_ignore_root_state(character);
			if (next == nullptr) {
				next = new state(d_depth + 1);
				d_success[character].reset(next);
			}
			return next;
		}

		void add_emit(string_ref_type keyword, unsigned index) {
			d_emits.insert(std::make_pair(keyword, index));
		}

		void add_emit(const string_collection& emits) {
			for (const auto& e : emits) {
				string_type str(e.first);
				add_emit(str, e.second);
			}
		}

		string_collection get_emits() const { return d_emits; }

		ptr failure() const { return d_failure; }

		void set_failure(ptr fail_state) { d_failure = fail_state; }

		state_collection get_states() const {
			state_collection result;
			for (auto it = d_success.cbegin(); it != d_success.cend(); ++it) {
				result.push_back(it->second.get());
			}
			return state_collection(result);
		}

		transition_collection get_transitions() const {
			transition_collection result;
			for (auto it = d_success.cbegin(); it != d_success.cend(); ++it) {
				result.push_back(it->first);
			}
			return transition_collection(result);
		}

	private:
		ptr next_state(char character, bool ignore_root_state) const {
			ptr result = nullptr;
			auto found = d_success.find(character);
			if (found != d_success.end()) {
				result = found->second.get();
			} else if (!ignore_root_state && d_root != nullptr) {
				result = d_root;
			}
			return result;
		}
	};

	class trie {
	public:
		using string_type = std::basic_string <char>;
		using string_ref_type = std::basic_string<char>&;

		typedef state         state_type;
		typedef state*        state_ptr_type;

	private:
		std::unique_ptr<state_type> d_root;
		bool                        d_constructed_failure_states;
		unsigned                    d_num_keywords = 0;

	public:
		trie() : d_root(new state_type())
			, d_constructed_failure_states(false) {}

		void insert(string_type keyword) {
			if (keyword.empty())
				return;
			state_ptr_type cur_state = d_root.get();
			for (const auto& ch : keyword) {
				cur_state = cur_state->add_state(ch);
			}
			cur_state->add_emit(keyword, d_num_keywords++);
			d_constructed_failure_states = false;
		}

		bool contains(string_type text) {
			check_construct_failure_states();
			state_ptr_type cur_state = d_root.get();
			for (auto c : text) {
				cur_state = get_state(cur_state, c);
        auto emits = cur_state->get_emits();
        if (!emits.empty()) {
          return true;
        }
			}
      return false;
		}

	private:

		state_ptr_type get_state(state_ptr_type cur_state, char c) const {
			state_ptr_type result = cur_state->next_state(c);
			while (result == nullptr) {
				cur_state = cur_state->failure();
				result = cur_state->next_state(c);
			}
			return result;
		}

		void check_construct_failure_states() {
			if (!d_constructed_failure_states) {
				construct_failure_states();
			}
		}

		void construct_failure_states() {
			std::queue<state_ptr_type> q;
			for (auto& depth_one_state : d_root->get_states()) {
				depth_one_state->set_failure(d_root.get());
				q.push(depth_one_state);
			}
			d_constructed_failure_states = true;

			while (!q.empty()) {
				auto cur_state = q.front();
				for (const auto& transition : cur_state->get_transitions()) {
					state_ptr_type target_state = cur_state->next_state(transition);
					q.push(target_state);

					state_ptr_type trace_failure_state = cur_state->failure();
					while (trace_failure_state->next_state(transition) == nullptr) {
						trace_failure_state = trace_failure_state->failure();
					}
					state_ptr_type new_failure_state = trace_failure_state->next_state(transition);
					target_state->set_failure(new_failure_state);
					target_state->add_emit(new_failure_state->get_emits());
				}
				q.pop();
			}
		}
};

} // namespace aho_corasick

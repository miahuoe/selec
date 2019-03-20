# intr
Same as [entr](http://eradman.com/entrproject/) but it runs arbitrary commands as you TYPE
I basically took [fzf](https://github.com/junegunn/fzf) and attempted to make it more minimal. And there it is.

# Usage
Mimic fzf: `intr find . \| grep {}`
Play with regex: `intr grep -E {} < file.txt`

# TODOs
- packaging
- multiselect
- "slient" commands
- extend readme
- ...

# Thank you
[entr](http://eradman.com/entrproject/) and [fzf](https://github.com/junegunn/fzf) are great tools. Thanks devs!

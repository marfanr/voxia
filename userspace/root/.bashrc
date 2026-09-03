# voxia bashrc
export TERM=xterm
export HOME=/root
export PATH=/usr/bin:/bin:/sbin:/usr/sbin
force_color_prompt=yes

if [ "$force_color_prompt" = yes ]; then
    export PS1='\[\033[01;12m\]\u@voxia\[\033[00m\]:\[\033[01;34m\]\w\[\033[00m\]# '
  #  alias ls='ls --color=auto'
    alias grep='grep --color=auto'
else
    export PS1='\u@voxia:\w# '
fi

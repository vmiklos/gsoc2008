#!/bin/sh

USAGE='[help|start|bad|good|skip|next|reset|visualize|replay|log|run]'
LONG_USAGE='git bisect help
        print this long help message.
git bisect start [<bad> [<good>...]] [--] [<pathspec>...]
        reset bisect state and start bisection.
git bisect bad [<rev>]
        mark <rev> a known-bad revision.
git bisect good [<rev>...]
        mark <rev>... known-good revisions.
git bisect skip [<rev>...]
        mark <rev>... untestable revisions.
git bisect next
        find next bisection to test and check it out.
git bisect reset [<branch>]
        finish bisection search and go back to branch.
git bisect visualize
        show bisect status in gitk.
git bisect replay <logfile>
        replay bisection log.
git bisect log
        show bisect log.
git bisect run <cmd>...
        use <cmd>... to automatically bisect.

Please use "git help bisect" to get the full man page.'

OPTIONS_SPEC=
. git-sh-setup
require_work_tree

_x40='[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'
_x40="$_x40$_x40$_x40$_x40$_x40$_x40$_x40$_x40"

sq() {
	@@PERL@@ -e '
		for (@ARGV) {
			s/'\''/'\'\\\\\'\''/g;
			print " '\''$_'\''";
		}
		print "\n";
	' "$@"
}

bisect_autostart() {
	test -s "$GIT_DIR/BISECT_START" || {
		echo >&2 'You need to start by "git bisect start"'
		if test -t 0
		then
			echo >&2 -n 'Do you want me to do it for you [Y/n]? '
			read yesno
			case "$yesno" in
			[Nn]*)
				exit ;;
			esac
			bisect_start
		else
			exit 1
		fi
	}
}

bisect_start() {
	#
	# Verify HEAD.
	#
	head=$(GIT_DIR="$GIT_DIR" git symbolic-ref -q HEAD) ||
	head=$(GIT_DIR="$GIT_DIR" git rev-parse --verify HEAD) ||
	die "Bad HEAD - I need a HEAD"

	#
	# Check if we are bisecting.
	#
	start_head=''
	if test -s "$GIT_DIR/BISECT_START"
	then
		# Reset to the rev from where we started.
		start_head=$(cat "$GIT_DIR/BISECT_START")
		git checkout "$start_head" || exit
	else
		# Get rev from where we start.
		case "$head" in
		refs/heads/*|$_x40)
			# This error message should only be triggered by
			# cogito usage, and cogito users should understand
			# it relates to cg-seek.
			[ -s "$GIT_DIR/head-name" ] &&
				die "won't bisect on seeked tree"
			start_head="${head#refs/heads/}"
			;;
		*)
			die "Bad HEAD - strange symbolic ref"
			;;
		esac
	fi

	#
	# Get rid of any old bisect state.
	#
	bisect_clean_state || exit

	#
	# Check for one bad and then some good revisions.
	#
	has_double_dash=0
	for arg; do
	    case "$arg" in --) has_double_dash=1; break ;; esac
	done
	orig_args=$(sq "$@")
	bad_seen=0
	eval=''
	while [ $# -gt 0 ]; do
	    arg="$1"
	    case "$arg" in
	    --)
		shift
		break
		;;
	    *)
		rev=$(git rev-parse -q --verify "$arg^{commit}") || {
		    test $has_double_dash -eq 1 &&
		        die "'$arg' does not appear to be a valid revision"
		    break
		}
		case $bad_seen in
		0) state='bad' ; bad_seen=1 ;;
		*) state='good' ;;
		esac
		eval="$eval bisect_write '$state' '$rev' 'nolog'; "
		shift
		;;
	    esac
	done

	#
	# Change state.
	# In case of mistaken revs or checkout error, or signals received,
	# "bisect_auto_next" below may exit or misbehave.
	# We have to trap this to be able to clean up using
	# "bisect_clean_state".
	#
	trap 'bisect_clean_state' 0
	trap 'exit 255' 1 2 3 15

	#
	# Write new start state.
	#
	echo "$start_head" >"$GIT_DIR/BISECT_START" &&
	sq "$@" >"$GIT_DIR/BISECT_NAMES" &&
	eval "$eval" &&
	echo "git bisect start$orig_args" >>"$GIT_DIR/BISECT_LOG" || exit
	#
	# Check if we can proceed to the next bisect state.
	#
	bisect_auto_next

	trap '-' 0
}

bisect_write() {
	state="$1"
	rev="$2"
	nolog="$3"
	case "$state" in
		bad)		tag="$state" ;;
		good|skip)	tag="$state"-"$rev" ;;
		*)		die "Bad bisect_write argument: $state" ;;
	esac
	git update-ref "refs/bisect/$tag" "$rev" || exit
	echo "# $state: $(git show-branch $rev)" >>"$GIT_DIR/BISECT_LOG"
	test -n "$nolog" || echo "git bisect $state $rev" >>"$GIT_DIR/BISECT_LOG"
}

bisect_state() {
	bisect_autostart
	state=$1
	case "$#,$state" in
	0,*)
		die "Please call 'bisect_state' with at least one argument." ;;
	1,bad|1,good|1,skip)
		rev=$(git rev-parse --verify HEAD) ||
			die "Bad rev input: HEAD"
		bisect_write "$state" "$rev" ;;
	2,bad|*,good|*,skip)
		shift
		eval=''
		for rev in "$@"
		do
			sha=$(git rev-parse --verify "$rev^{commit}") ||
				die "Bad rev input: $rev"
			eval="$eval bisect_write '$state' '$sha'; "
		done
		eval "$eval" ;;
	*,bad)
		die "'git bisect bad' can take only one argument." ;;
	*)
		usage ;;
	esac
	bisect_auto_next
}

bisect_next_check() {
	missing_good= missing_bad=
	git show-ref -q --verify refs/bisect/bad || missing_bad=t
	test -n "$(git for-each-ref "refs/bisect/good-*")" || missing_good=t

	case "$missing_good,$missing_bad,$1" in
	,,*)
		: have both good and bad - ok
		;;
	*,)
		# do not have both but not asked to fail - just report.
		false
		;;
	t,,good)
		# have bad but not good.  we could bisect although
		# this is less optimum.
		echo >&2 'Warning: bisecting only with a bad commit.'
		if test -t 0
		then
			printf >&2 'Are you sure [Y/n]? '
			read yesno
			case "$yesno" in [Nn]*) exit 1 ;; esac
		fi
		: bisect without good...
		;;
	*)
		THEN=''
		test -s "$GIT_DIR/BISECT_START" || {
			echo >&2 'You need to start by "git bisect start".'
			THEN='then '
		}
		echo >&2 'You '$THEN'need to give me at least one good' \
			'and one bad revisions.'
		echo >&2 '(You can use "git bisect bad" and' \
			'"git bisect good" for that.)'
		exit 1 ;;
	esac
}

bisect_auto_next() {
	bisect_next_check && bisect_next || :
}

eval_rev_list() {
	_eval="$1"

	eval $_eval
	res=$?

	if [ $res -ne 0 ]; then
		echo >&2 "'git rev-list --bisect-vars' failed:"
		echo >&2 "maybe you mistake good and bad revs?"
		exit $res
	fi

	return $res
}

filter_skipped() {
	_eval="$1"
	_skip="$2"

	if [ -z "$_skip" ]; then
		eval_rev_list "$_eval"
		return
	fi

	# Let's parse the output of:
	# "git rev-list --bisect-vars --bisect-all ..."
	eval_rev_list "$_eval" | while read hash line
	do
		case "$VARS,$FOUND,$TRIED,$hash" in
			# We display some vars.
			1,*,*,*) echo "$hash $line" ;;

			# Split line.
			,*,*,---*) ;;

			# We had nothing to search.
			,,,bisect_rev*)
				echo "bisect_rev="
				VARS=1
				;;

			# We did not find a good bisect rev.
			# This should happen only if the "bad"
			# commit is also a "skip" commit.
			,,*,bisect_rev*)
				echo "bisect_rev=$TRIED"
				VARS=1
				;;

			# We are searching.
			,,*,*)
				TRIED="${TRIED:+$TRIED|}$hash"
				case "$_skip" in
				*$hash*) ;;
				*)
					echo "bisect_rev=$hash"
					echo "bisect_tried=\"$TRIED\""
					FOUND=1
					;;
				esac
				;;

			# We have already found a rev to be tested.
			,1,*,bisect_rev*) VARS=1 ;;
			,1,*,*) ;;

			# ???
			*) die "filter_skipped error " \
			    "VARS: '$VARS' " \
			    "FOUND: '$FOUND' " \
			    "TRIED: '$TRIED' " \
			    "hash: '$hash' " \
			    "line: '$line'"
			;;
		esac
	done
}

exit_if_skipped_commits () {
	_tried=$1
	if expr "$_tried" : ".*[|].*" > /dev/null ; then
		echo "There are only 'skip'ped commit left to test."
		echo "The first bad commit could be any of:"
		echo "$_tried" | tr '[|]' '[\012]'
		echo "We cannot bisect more!"
		exit 2
	fi
}

bisect_next() {
	case "$#" in 0) ;; *) usage ;; esac
	bisect_autostart
	bisect_next_check good

	skip=$(git for-each-ref --format='%(objectname)' \
		"refs/bisect/skip-*" | tr '\012' ' ') || exit

	BISECT_OPT=''
	test -n "$skip" && BISECT_OPT='--bisect-all'

	bad=$(git rev-parse --verify refs/bisect/bad) &&
	good=$(git for-each-ref --format='^%(objectname)' \
		"refs/bisect/good-*" | tr '\012' ' ') &&
	eval="git rev-list --bisect-vars $BISECT_OPT $good $bad --" &&
	eval="$eval $(cat "$GIT_DIR/BISECT_NAMES")" &&
	eval=$(filter_skipped "$eval" "$skip") &&
	eval "$eval" || exit

	if [ -z "$bisect_rev" ]; then
		echo "$bad was both good and bad"
		exit 1
	fi
	if [ "$bisect_rev" = "$bad" ]; then
		exit_if_skipped_commits "$bisect_tried"
		echo "$bisect_rev is first bad commit"
		git diff-tree --pretty $bisect_rev
		exit 0
	fi

	# We should exit here only if the "bad"
	# commit is also a "skip" commit (see above).
	exit_if_skipped_commits "$bisect_rev"

	echo "Bisecting: $bisect_nr revisions left to test after this"
	git checkout -q "$bisect_rev" || exit
	git show-branch "$bisect_rev"
}

bisect_visualize() {
	bisect_next_check fail

	if test $# = 0
	then
		case "${DISPLAY+set}${MSYSTEM+set}${SECURITYSESSIONID+set}" in
		'')	set git log ;;
		set*)	set gitk ;;
		esac
	else
		case "$1" in
		git*|tig) ;;
		-*)	set git log "$@" ;;
		*)	set git "$@" ;;
		esac
	fi

	not=$(git for-each-ref --format='%(refname)' "refs/bisect/good-*")
	eval '"$@"' refs/bisect/bad --not $not -- $(cat "$GIT_DIR/BISECT_NAMES")
}

bisect_reset() {
	test -s "$GIT_DIR/BISECT_START" || {
		echo "We are not bisecting."
		return
	}
	case "$#" in
	0) branch=$(cat "$GIT_DIR/BISECT_START") ;;
	1) git show-ref --verify --quiet -- "refs/heads/$1" ||
	       die "$1 does not seem to be a valid branch"
	   branch="$1" ;;
	*)
	    usage ;;
	esac
	git checkout "$branch" && bisect_clean_state
}

bisect_clean_state() {
	# There may be some refs packed during bisection.
	git for-each-ref --format='%(refname) %(objectname)' refs/bisect/\* |
	while read ref hash
	do
		git update-ref -d $ref $hash || exit
	done
	rm -f "$GIT_DIR/BISECT_LOG" &&
	rm -f "$GIT_DIR/BISECT_NAMES" &&
	rm -f "$GIT_DIR/BISECT_RUN" &&
	# Cleanup head-name if it got left by an old version of git-bisect
	rm -f "$GIT_DIR/head-name" &&

	rm -f "$GIT_DIR/BISECT_START"
}

bisect_replay () {
	test -r "$1" || die "cannot read $1 for replaying"
	bisect_reset
	while read git bisect command rev
	do
		test "$git $bisect" = "git bisect" -o "$git" = "git-bisect" || continue
		if test "$git" = "git-bisect"; then
			rev="$command"
			command="$bisect"
		fi
		case "$command" in
		start)
			cmd="bisect_start $rev"
			eval "$cmd" ;;
		good|bad|skip)
			bisect_write "$command" "$rev" ;;
		*)
			die "?? what are you talking about?" ;;
		esac
	done <"$1"
	bisect_auto_next
}

bisect_run () {
    bisect_next_check fail

    while true
    do
      echo "running $@"
      "$@"
      res=$?

      # Check for really bad run error.
      if [ $res -lt 0 -o $res -ge 128 ]; then
	  echo >&2 "bisect run failed:"
	  echo >&2 "exit code $res from '$@' is < 0 or >= 128"
	  exit $res
      fi

      # Find current state depending on run success or failure.
      # A special exit code of 125 means cannot test.
      if [ $res -eq 125 ]; then
	  state='skip'
      elif [ $res -gt 0 ]; then
	  state='bad'
      else
	  state='good'
      fi

      # We have to use a subshell because "bisect_state" can exit.
      ( bisect_state $state > "$GIT_DIR/BISECT_RUN" )
      res=$?

      cat "$GIT_DIR/BISECT_RUN"

      if grep "first bad commit could be any of" "$GIT_DIR/BISECT_RUN" \
		> /dev/null; then
	  echo >&2 "bisect run cannot continue any more"
	  exit $res
      fi

      if [ $res -ne 0 ]; then
	  echo >&2 "bisect run failed:"
	  echo >&2 "'bisect_state $state' exited with error code $res"
	  exit $res
      fi

      if grep "is first bad commit" "$GIT_DIR/BISECT_RUN" > /dev/null; then
	  echo "bisect run success"
	  exit 0;
      fi

    done
}


case "$#" in
0)
    usage ;;
*)
    cmd="$1"
    shift
    case "$cmd" in
    help)
        git bisect -h ;;
    start)
        bisect_start "$@" ;;
    bad|good|skip)
        bisect_state "$cmd" "$@" ;;
    next)
        # Not sure we want "next" at the UI level anymore.
        bisect_next "$@" ;;
    visualize|view)
	bisect_visualize "$@" ;;
    reset)
        bisect_reset "$@" ;;
    replay)
	bisect_replay "$@" ;;
    log)
	cat "$GIT_DIR/BISECT_LOG" ;;
    run)
        bisect_run "$@" ;;
    *)
        usage ;;
    esac
esac

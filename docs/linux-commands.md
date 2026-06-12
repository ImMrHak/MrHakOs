# Linux Commands Guide for Developers

This guide covers common Linux commands used while working on MrHakOS from the host
Linux system. These are normal Linux shell commands, not commands inside the
MrHakOS kernel terminal.

The examples are intentionally safe. Practice in a temporary directory first:

```bash
practice_dir="$(mktemp -d /tmp/linux-command-practice.XXXXXX)"
cd "$practice_dir"
printf 'hello\nsecond line\nlast line\n' > file1
mkdir -p folder1
printf 'text inside a file\n' > folder1/file.txt
```

Before deleting, changing permissions, or changing ownership, check where you are:

```bash
pwd
ls -la
```

## Copy Files And Folders

### `cp file1 file2`

- What it does: Copies one file to another file path.
- When to use it: Use it when you want a backup or a second copy of a file.
- Safe example:

```bash
cp file1 file2
```

- Warning: If `file2` already exists, it can be overwritten. Use `cp -i file1 file2`
  if you want Linux to ask before overwriting.

### `cp -r folder1 folder2`

- What it does: Copies a folder and everything inside it.
- When to use it: Use it to duplicate a directory tree.
- Safe example:

```bash
cp -r folder1 folder2
```

- Warning: Copying large folders can take time and disk space. If `folder2` already
  exists, the copy may be placed inside it.

## Move Or Rename

### `mv old new`

- What it does: Moves or renames a file or folder.
- When to use it: Use it to rename a file, move a file into another folder, or rename
  a directory.
- Safe example:

```bash
mv file2 renamed-file
```

- Warning: If the destination exists, `mv` may overwrite it depending on the target.
  Use `mv -i old new` when you want a confirmation prompt.

## Delete Files And Folders

### `rm file.txt`

- What it does: Deletes one file.
- When to use it: Use it when you are sure a file is no longer needed.
- Safe example:

```bash
rm renamed-file
```

- Warning: Deleted files usually do not go to a desktop trash folder when removed from
  the terminal.

### `rm -r folder`

- What it does: Deletes a folder and its contents recursively.
- When to use it: Use it for folders that contain files or other folders.
- Safe example:

```bash
rm -r folder2
```

- Warning: This can delete many files. Always run `pwd` and `ls` first.

### `rm -rf folder`

- What it does: Force-deletes a folder recursively. `-r` means recursive, and `-f`
  means force without prompts.
- When to use it: Use it only for generated, disposable directories that you can
  recreate.
- Safe example:

```bash
rm -rf "$practice_dir/disposable-build-output"
```

- Warning: This is dangerous. A wrong path can permanently delete important data.
  Never run `rm -rf /`, `rm -rf ~`, or `rm -rf *` unless you fully understand the
  current directory and the expanded paths.

## Edit And Read Files

### `nano file.txt`

- What it does: Opens a text file in the Nano terminal editor.
- When to use it: Use it for quick edits to small text files.
- Safe example:

```bash
nano folder1/file.txt
```

- Warning: Saving changes overwrites the file. Use `Ctrl+O` to save, `Enter` to
  confirm, and `Ctrl+X` to exit.

### `less file.txt`

- What it does: Opens a file page by page without editing it.
- When to use it: Use it for reading long files, logs, or command output saved to a
  file.
- Safe example:

```bash
less folder1/file.txt
```

- Warning: `less` is read-only, but it can look like it is "stuck" if you are new to
  it. Press `q` to quit.

### `head file.txt`

- What it does: Shows the first lines of a file.
- When to use it: Use it to quickly inspect the beginning of a file.
- Safe example:

```bash
head file1
```

- Warning: This is safe and read-only.

### `tail file.txt`

- What it does: Shows the last lines of a file.
- When to use it: Use it to inspect the end of logs or output files.
- Safe example:

```bash
tail file1
```

- Warning: This is safe and read-only.

### `tail -f file.log`

- What it does: Shows the end of a file and keeps waiting for new lines.
- When to use it: Use it for live logs while another process is writing to the file.
- Safe example:

```bash
printf 'service started\n' > file.log
tail -f file.log
```

- Warning: `tail -f` keeps running. Press `Ctrl+C` to stop it.

## Find Files And Folders

### `find . -name "file.txt"`

- What it does: Searches under the current directory for paths named `file.txt`.
- When to use it: Use it when you know a file name but not its location.
- Safe example:

```bash
find . -name "file.txt"
```

- Warning: Quote names with special characters so the shell does not expand them.

### `find . -type f`

- What it does: Lists all regular files under the current directory.
- When to use it: Use it to see every file in a project tree.
- Safe example:

```bash
find . -type f
```

- Warning: On large trees this can print a lot of output.

### `find . -type d`

- What it does: Lists all directories under the current directory.
- When to use it: Use it to inspect a folder layout.
- Safe example:

```bash
find . -type d
```

- Warning: This is read-only, but it can produce large output in big projects.

## Search Text

### `grep "text" file.txt`

- What it does: Searches for matching text in one file.
- When to use it: Use it to find a word, setting, symbol, or log message.
- Safe example:

```bash
grep "text" folder1/file.txt
```

- Warning: Search patterns can be regular expressions. Characters like `.`, `*`, and
  `[` may have special meaning.

### `grep -r "text" .`

- What it does: Searches recursively under the current directory.
- When to use it: Use it to search a project folder.
- Safe example:

```bash
grep -r "text" .
```

- Warning: Recursive searches can be noisy and slow in large directories. Avoid
  searching from `/`.

### `grep -rn "text" .`

- What it does: Searches recursively and prints matching line numbers.
- When to use it: Use it when you want to jump directly to the matching line.
- Safe example:

```bash
grep -rn "text" .
```

- Warning: This is read-only, but it can include generated files unless you search a
  focused directory. For code search, `rg "text"` is often faster if ripgrep is
  installed.

## Permissions And Ownership

### `chmod +x script.sh`

- What it does: Adds executable permission to a script.
- When to use it: Use it before running a local script as `./script.sh`.
- Safe example:

```bash
printf '#!/usr/bin/env bash\nprintf "hello\\n"\n' > script.sh
chmod +x script.sh
./script.sh
```

- Warning: Only execute scripts you trust and have read.

### `chmod 755 file.sh`

- What it does: Sets permissions to owner read/write/execute, group read/execute, and
  others read/execute.
- When to use it: Use it for scripts or tools that should be executable.
- Safe example:

```bash
chmod 755 script.sh
```

- Warning: This allows other local users to read and execute the file.

### `chmod 644 file.txt`

- What it does: Sets permissions to owner read/write, group read, and others read.
- When to use it: Use it for normal text files that should not be executable.
- Safe example:

```bash
chmod 644 folder1/file.txt
```

- Warning: This allows other local users to read the file. Do not use it on secrets.

### `sudo chown -R $USER:$USER folder`

- What it does: Changes ownership of a folder and everything under it to your current
  user and group.
- When to use it: Use it when a project folder was accidentally created or modified by
  `root`, usually after using `sudo`.
- Safe example:

```bash
sudo chown -R "$USER:$USER" "$practice_dir/folder1"
```

- Warning: `sudo` runs with administrator privileges, and `-R` changes ownership
  recursively. A wrong path can break system files or another project. Never run this
  on `/`, `/usr`, `/etc`, or your whole home directory unless you know exactly why.

## Safe Cleanup

When you are done with the temporary practice directory, delete only that directory:

```bash
cd /
rm -rf "$practice_dir"
```

Before pressing Enter, confirm that `practice_dir` starts with
`/tmp/linux-command-practice.`.

